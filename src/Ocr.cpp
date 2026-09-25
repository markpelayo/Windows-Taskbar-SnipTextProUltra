#include "Ocr.h"

#include "Log.h"
#include "Util.h"

#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.graphics.imaging.h>
#include <windows.media.ocr.h>

#include <algorithm>
#include <atomic>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
using Microsoft::WRL::Wrappers::HStringReference;

namespace ABIF  = ABI::Windows::Foundation;
namespace ABIC  = ABI::Windows::Foundation::Collections;
namespace ABIG  = ABI::Windows::Graphics::Imaging;
namespace ABIO  = ABI::Windows::Media::Ocr;

namespace ocr {
namespace {

// Declared here rather than relying on the SDK header, which has moved
// between namespaces across SDK versions. The IID is stable.
MIDL_INTERFACE("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")
ISnipMemoryBufferByteAccess : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

// Initialises the multithreaded apartment for the duration of one call and
// uninitialises it on every exit path, including an early return.
class ApartmentScope {
public:
    ApartmentScope() {
        HRESULT hr = ::RoInitialize(RO_INIT_MULTITHREADED);
        // RPC_E_CHANGED_MODE means this thread is already in an apartment we
        // must not tear down. Both are usable; only the first is ours.
        owned_ = SUCCEEDED(hr);
        usable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ApartmentScope(const ApartmentScope&) = delete;
    ApartmentScope& operator=(const ApartmentScope&) = delete;
    ~ApartmentScope() { if (owned_) ::RoUninitialize(); }
    bool usable() const { return usable_; }
private:
    bool owned_  = false;
    bool usable_ = false;
};

// Blocks until a WinRT async operation completes. OCR runs on the capture
// worker thread, which has nothing else to do, so blocking is simpler and
// cheaper than threading a continuation through the pipeline.
// Owns an event handle through a shared_ptr, so the completion callback and
// the waiter each hold a reference and whichever finishes last closes it. A
// raw handle captured by value would be closed by the timeout path while the
// callback was still holding it.
struct SharedEvent {
    HANDLE handle = nullptr;
    SharedEvent() : handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    SharedEvent(const SharedEvent&) = delete;
    SharedEvent& operator=(const SharedEvent&) = delete;
    ~SharedEvent() { if (handle) ::CloseHandle(handle); }
};

HRESULT AwaitOcrResult(ABIF::IAsyncOperation<ABIO::OcrResult*>* operation,
                       ABIO::IOcrResult** result) {
    if (!operation || !result) return E_POINTER;

    auto done = std::make_shared<SharedEvent>();
    if (!done->handle) return E_FAIL;

    HRESULT hr = operation->put_Completed(
        Callback<ABIF::IAsyncOperationCompletedHandler<ABIO::OcrResult*>>(
            [done](ABIF::IAsyncOperation<ABIO::OcrResult*>*, ABIF::AsyncStatus) -> HRESULT {
                ::SetEvent(done->handle);
                return S_OK;
            }).Get());
    if (FAILED(hr)) return hr;

    HANDLE signal = done->handle;

    // A generous ceiling. OCR on a 4K capture is well under a second; this
    // exists only so a wedged engine cannot hang the process forever.
    if (::WaitForSingleObject(signal, 30000) != WAIT_OBJECT_0) return E_ABORT;

    return operation->GetResults(result);
}

ComPtr<ABIO::IOcrEngineStatics> EngineStatics() {
    ComPtr<ABIO::IOcrEngineStatics> statics;
    HRESULT hr = ::RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_Media_Ocr_OcrEngine).Get(),
        IID_PPV_ARGS(&statics));
    if (FAILED(hr)) return nullptr;
    return statics;
}

// Wraps our BGRA pixels in a SoftwareBitmap without an intermediate encode.
// Going via PNG would cost a full compress-then-decompress round trip on
// every capture.
ComPtr<ABIG::ISoftwareBitmap> MakeSoftwareBitmap(const Bitmap& image) {
    ComPtr<ABIG::ISoftwareBitmapFactory> factory;
    if (FAILED(::RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap).Get(),
            IID_PPV_ARGS(&factory)))) {
        return nullptr;
    }

    ComPtr<ABIG::ISoftwareBitmap> bitmap;
    // Alpha is forced to 255 before we get here, so "premultiplied" and
    // "ignore" describe the same pixels; premultiplied is what OCR expects.
    if (FAILED(factory->CreateWithAlpha(ABIG::BitmapPixelFormat_Bgra8,
                                        image.Width(), image.Height(),
                                        ABIG::BitmapAlphaMode_Premultiplied, &bitmap)) ||
        !bitmap) {
        return nullptr;
    }

    ComPtr<ABIG::IBitmapBuffer> buffer;
    if (FAILED(bitmap->LockBuffer(ABIG::BitmapBufferAccessMode_Write, &buffer)) || !buffer) {
        return nullptr;
    }

    ComPtr<ABIF::IMemoryBuffer> memory;
    if (FAILED(buffer.As(&memory)) || !memory) return nullptr;

    ComPtr<ABIF::IMemoryBufferReference> reference;
    if (FAILED(memory->CreateReference(&reference)) || !reference) return nullptr;

    ComPtr<ISnipMemoryBufferByteAccess> access;
    if (FAILED(reference.As(&access)) || !access) return nullptr;

    BYTE*  target   = nullptr;
    UINT32 capacity = 0;
    if (FAILED(access->GetBuffer(&target, &capacity)) || !target) return nullptr;

    ABIG::BitmapPlaneDescription description{};
    if (FAILED(buffer->GetPlaneDescription(0, &description))) return nullptr;

    const int rows = (std::min)(image.Height(), description.Height);
    const int rowBytes = (std::min)(image.Stride(), description.Stride);
    const BYTE* source = static_cast<const BYTE*>(image.Bits());
    for (int y = 0; y < rows; ++y) {
        const size_t offset = static_cast<size_t>(description.StartIndex)
                            + static_cast<size_t>(y) * description.Stride;
        if (offset + rowBytes > capacity) break;
        ::memcpy(target + offset, source + static_cast<size_t>(y) * image.Stride(), rowBytes);
    }

    // The reference and buffer must be released before the bitmap is handed
    // to the engine, or RecognizeAsync fails with "buffer is locked".
    access.Reset();
    reference.Reset();
    memory.Reset();
    buffer.Reset();
    return bitmap;
}

std::wstring HStringToWide(HSTRING value) {
    if (!value) return std::wstring();
    UINT32 length = 0;
    const wchar_t* raw = ::WindowsGetStringRawBuffer(value, &length);
    return raw ? std::wstring(raw, length) : std::wstring();
}

// Windows OCR already returns lines, but it splits a visual row wherever the
// spacing is wide — table columns, for instance. Running the same
// row-bucketing pass the macOS original applies to raw observations merges
// those back together, so the two ports agree on what a "line" is.
void BucketIntoRows(std::vector<OcrLine>& observations, std::vector<OcrLine>& out) {
    // Sort on a SINGLE key so the comparator is a valid strict weak ordering.
    // A threshold-based comparator ("same row, so compare X") is not
    // transitive, and in C++ that is undefined behaviour inside std::sort,
    // not merely a wrong answer. Row grouping is therefore a separate linear
    // pass afterwards, never part of the comparison.
    std::stable_sort(observations.begin(), observations.end(),
                     [](const OcrLine& a, const OcrLine& b) { return a.midY > b.midY; });

    std::vector<std::vector<const OcrLine*>> rows;
    double rowMidY   = 0.0;
    double rowHeight = 0.0;

    for (const OcrLine& observation : observations) {
        if (!rows.empty() &&
            std::fabs(rowMidY - observation.midY)
                < (std::max)(rowHeight, observation.height) * 0.5) {
            rows.back().push_back(&observation);
            // Compare against the row's running MEAN rather than its first
            // member, so a tall heading can't widen the tolerance and swallow
            // the lines beneath it. The count is taken after the append.
            const double count = static_cast<double>(rows.back().size());
            rowMidY += (observation.midY - rowMidY) / count;
            rowHeight = (std::max)(rowHeight, observation.height);
        } else {
            rows.push_back({ &observation });
            rowMidY   = observation.midY;   // reset, not merged
            rowHeight = observation.height;
        }
    }

    out.clear();
    out.reserve(rows.size());
    for (std::vector<const OcrLine*>& row : rows) {
        std::stable_sort(row.begin(), row.end(),
                         [](const OcrLine* a, const OcrLine* b) { return a->minX < b->minX; });

        OcrLine merged;
        double midYSum = 0.0;
        size_t contributing = 0;
        bool first = true;
        for (const OcrLine* part : row) {
            // A part with no text contributes no geometry either, so it must
            // not be counted in the mean below.
            if (part->text.empty()) continue;
            ++contributing;
            if (!merged.text.empty()) merged.text.append(L" ");
            merged.text.append(part->text);
            if (first) {
                merged.minX = part->minX;
                merged.maxX = part->maxX;
                merged.height = part->height;
                first = false;
            } else {
                merged.minX   = (std::min)(merged.minX, part->minX);
                merged.maxX   = (std::max)(merged.maxX, part->maxX);
                merged.height = (std::max)(merged.height, part->height);
            }
            midYSum += part->midY;
        }
        if (merged.text.empty() || contributing == 0) continue;   // row dropped entirely
        merged.midY = midYSum / static_cast<double>(contributing);
        out.push_back(std::move(merged));
    }
}


// --- preparing the image for the engine ------------------------------------
//
// Windows.Media.Ocr is a language recogniser, not a shape recogniser: it
// scores candidate regions against a lexicon and discards what does not look
// like words. That makes it good at prose and noticeably worse than Apple's
// Vision at strings with no dictionary word in them — serial numbers, hashes,
// punctuation runs.
//
// Nothing here changes that. What it does change is the cases where the
// engine was simply struggling to SEE the text: light-on-dark, or small.
// Those are worth a second attempt, because a second attempt is cheap
// compared to the capture the user has already taken.

double MeanLuminance(const Bitmap& image) {
    if (!image.IsValid()) return 255.0;
    const BYTE* pixels = static_cast<const BYTE*>(image.Bits());
    const size_t total = static_cast<size_t>(image.Width()) * image.Height();
    if (total == 0) return 255.0;

    // Every 7th pixel: a fair estimate of the overall brightness at a
    // seventh of the cost. An odd stride keeps the sample walking across
    // columns rather than settling into one.
    unsigned long long sum = 0;
    size_t counted = 0;
    for (size_t i = 0; i < total; i += 7) {
        const BYTE* p = pixels + i * 4;
        sum += static_cast<unsigned long long>(p[2]) * 54
             + static_cast<unsigned long long>(p[1]) * 183
             + static_cast<unsigned long long>(p[0]) * 19;   // /256 below
        ++counted;
    }
    return counted ? static_cast<double>(sum) / counted / 256.0 : 255.0;
}

// Light text on a dark background, inverted so it becomes dark on light —
// the orientation every OCR engine is trained hardest on.
std::unique_ptr<Bitmap> MakeInverted(const Bitmap& image) {
    auto out = Bitmap::Create(image.Width(), image.Height());
    if (!out) return nullptr;
    const BYTE* from = static_cast<const BYTE*>(image.Bits());
    BYTE* to = static_cast<BYTE*>(out->Bits());
    const size_t total = static_cast<size_t>(image.Width()) * image.Height();
    for (size_t i = 0; i < total; ++i) {
        to[i * 4 + 0] = static_cast<BYTE>(255 - from[i * 4 + 0]);
        to[i * 4 + 1] = static_cast<BYTE>(255 - from[i * 4 + 1]);
        to[i * 4 + 2] = static_cast<BYTE>(255 - from[i * 4 + 2]);
        to[i * 4 + 3] = 255;
    }
    return out;
}

// Small text is the other thing the engine gives up on. Doubling it costs
// four times the pixels and often turns nothing into something.
std::unique_ptr<Bitmap> MakeUpscaled(const Bitmap& image, int factor) {
    const int width  = image.Width() * factor;
    const int height = image.Height() * factor;
    auto out = Bitmap::Create(width, height);
    if (!out || !out->MemoryDC() || !image.MemoryDC()) return nullptr;
    ::SetStretchBltMode(out->MemoryDC(), HALFTONE);
    ::SetBrushOrgEx(out->MemoryDC(), 0, 0, nullptr);
    ::StretchBlt(out->MemoryDC(), 0, 0, width, height,
                 image.MemoryDC(), 0, 0, image.Width(), image.Height(), SRCCOPY);
    // GDI batches drawing per thread, and everything downstream reads these
    // bytes directly rather than through GDI. Without the flush the reader
    // can see the bitmap before the blit has landed in it.
    ::GdiFlush();
    out->MakeOpaque();
    return out;
}

std::atomic<int> g_availability{ -1 };   // -1 unknown, 0 no, 1 yes

} // namespace

bool IsAvailable() {
    int cached = g_availability.load(std::memory_order_relaxed);
    if (cached >= 0) return cached == 1;

    ApartmentScope apartment;
    bool available = false;
    if (apartment.usable()) {
        if (ComPtr<ABIO::IOcrEngineStatics> statics = EngineStatics()) {
            ComPtr<ABIO::IOcrEngine> engine;
            available = SUCCEEDED(statics->TryCreateFromUserProfileLanguages(&engine)) && engine;
        }
    }
    g_availability.store(available ? 1 : 0, std::memory_order_relaxed);
    return available;
}

Result Recognize(const Bitmap& image) {
    Result result;
    if (!image.IsValid()) {
        result.failure = L"There was nothing to read.";
        return result;
    }

    ApartmentScope apartment;
    if (!apartment.usable()) {
        result.failure = L"Windows refused to start the text recognition runtime.";
        return result;
    }

    ComPtr<ABIO::IOcrEngineStatics> statics = EngineStatics();
    if (!statics) {
        result.engineAvailable = false;
        result.failure = L"Windows text recognition isn't available on this machine.";
        return result;
    }

    ComPtr<ABIO::IOcrEngine> engine;
    if (FAILED(statics->TryCreateFromUserProfileLanguages(&engine)) || !engine) {
        result.engineAvailable = false;
        result.failure = L"No OCR language is installed. Add one under "
                         L"Settings › Time & language › Language & region.";
        return result;
    }

    // The engine refuses images past a hard dimension limit. A full-screen
    // capture on a large display can exceed it, and refusing the capture
    // outright would be worse than reading a downscaled copy.
    UINT32 maxDimension = 0;
    statics->get_MaxImageDimension(&maxDimension);

    const Bitmap* source = &image;
    std::unique_ptr<Bitmap> scaled;
    if (maxDimension > 0 &&
        (static_cast<UINT32>(image.Width()) > maxDimension ||
         static_cast<UINT32>(image.Height()) > maxDimension)) {
        const double factor = (std::min)(static_cast<double>(maxDimension) / image.Width(),
                                         static_cast<double>(maxDimension) / image.Height());
        const int width  = (std::max)(1, static_cast<int>(image.Width() * factor));
        const int height = (std::max)(1, static_cast<int>(image.Height() * factor));
        scaled = Bitmap::Create(width, height);
        if (scaled && scaled->MemoryDC() && image.MemoryDC()) {
            ::SetStretchBltMode(scaled->MemoryDC(), HALFTONE);
            ::SetBrushOrgEx(scaled->MemoryDC(), 0, 0, nullptr);
            ::StretchBlt(scaled->MemoryDC(), 0, 0, width, height,
                         image.MemoryDC(), 0, 0, image.Width(), image.Height(), SRCCOPY);
            ::GdiFlush();   // see the note in MakeUpscaled
            scaled->MakeOpaque();
            source = scaled.get();
            LOG_DEBUG(util::Format(L"ocr: downscaled %dx%d to %dx%d for the engine limit",
                                   image.Width(), image.Height(), width, height));
        }
    }

    // One recognition pass over one prepared image. Returns the raw
    // observations; the caller decides which pass to keep.
    auto runPass = [&](const Bitmap& prepared, std::vector<OcrLine>& out) -> bool {
        out.clear();

        ComPtr<ABIG::ISoftwareBitmap> softwareBitmap = MakeSoftwareBitmap(prepared);
        if (!softwareBitmap) return false;

        ComPtr<ABIF::IAsyncOperation<ABIO::OcrResult*>> operation;
        if (FAILED(engine->RecognizeAsync(softwareBitmap.Get(), &operation)) || !operation) {
            return false;
        }

        ComPtr<ABIO::IOcrResult> recognized;
        if (FAILED(AwaitOcrResult(operation.Get(), recognized.GetAddressOf())) || !recognized) {
            return false;
        }

        ComPtr<ABIC::IVectorView<ABIO::OcrLine*>> engineLines;
        if (FAILED(recognized->get_Lines(&engineLines)) || !engineLines) return true;

        UINT32 lineCount = 0;
        engineLines->get_Size(&lineCount);
        if (lineCount == 0) return true;

        const double imageWidth  = static_cast<double>(prepared.Width());
        const double imageHeight = static_cast<double>(prepared.Height());
        out.reserve(lineCount);

        for (UINT32 i = 0; i < lineCount; ++i) {
            ComPtr<ABIO::IOcrLine> engineLine;
            if (FAILED(engineLines->GetAt(i, &engineLine)) || !engineLine) continue;

            ComPtr<ABIC::IVectorView<ABIO::OcrWord*>> words;
            if (FAILED(engineLine->get_Words(&words)) || !words) continue;

            UINT32 wordCount = 0;
            words->get_Size(&wordCount);
            if (wordCount == 0) continue;

            // Geometry is unioned across the words in X and maxed in height,
            // but the vertical centre is the MEAN of the words' centres — a
            // word with a descender must not drag the line's centre down.
            double minX = 0, maxX = 0, maxHeight = 0, midYSum = 0;
            size_t contributing = 0;
            std::wstring text;
            bool first = true;

            for (UINT32 w = 0; w < wordCount; ++w) {
                ComPtr<ABIO::IOcrWord> word;
                if (FAILED(words->GetAt(w, &word)) || !word) continue;

                ABIF::Rect box{};
                if (FAILED(word->get_BoundingRect(&box))) continue;

                HSTRING raw = nullptr;
                if (SUCCEEDED(word->get_Text(&raw))) {
                    if (!text.empty()) text.append(L" ");
                    text.append(HStringToWide(raw));
                    ::WindowsDeleteString(raw);
                }

                const double left   = box.X / imageWidth;
                const double right  = (box.X + box.Width) / imageWidth;
                const double height = box.Height / imageHeight;
                // Flip Y: the engine reports a top-left origin, everything
                // downstream assumes bottom-left.
                const double midY   = 1.0 - (box.Y + box.Height / 2.0) / imageHeight;

                if (first) {
                    minX = left; maxX = right; maxHeight = height;
                    first = false;
                } else {
                    minX = (std::min)(minX, left);
                    maxX = (std::max)(maxX, right);
                    maxHeight = (std::max)(maxHeight, height);
                }
                midYSum += midY;
                ++contributing;
            }

            if (first || text.empty() || contributing == 0) continue;

            OcrLine line;
            line.text   = std::move(text);
            line.minX   = minX;
            line.maxX   = maxX;
            line.height = maxHeight;
            line.midY   = midYSum / static_cast<double>(contributing);
            out.push_back(std::move(line));
        }
        return true;
    };

    auto characterCount = [](const std::vector<OcrLine>& lines) {
        size_t total = 0;
        for (const OcrLine& line : lines) total += line.text.size();
        return total;
    };

    // --- which images to try ---------------------------------------------
    // The plain capture first, because it is right the overwhelming majority
    // of the time. The other two exist for the engine's two blind spots, and
    // are only queued when the image actually has that problem — an ordinary
    // bright, roomy capture queues nothing else and costs exactly what it
    // always did.
    enum class Prep { AsCaptured, Inverted, Doubled, DoubledInverted };
    struct Attempt { Prep prep; const wchar_t* name; };

    std::vector<Attempt> attempts;
    attempts.push_back({ Prep::AsCaptured, L"as captured" });

    // Light text on a dark background. The engine reads dark-on-light
    // considerably better.
    const bool dark = MeanLuminance(*source) < 118.0;
    if (dark) attempts.push_back({ Prep::Inverted, L"inverted" });

    // Small text. Doubling it must not push the image back over the ceiling
    // the downscale above just brought it under — and that ceiling applies to
    // the LONG edge, so gating on the short edge alone would silently break
    // every wide, thin capture, which is the commonest shape of all: one line
    // of text dragged across a monitor.
    const int shortEdge = (std::min)(source->Width(), source->Height());
    const int longEdge  = (std::max)(source->Width(), source->Height());
    const bool canDouble = shortEdge < 700 &&
                           (maxDimension == 0 ||
                            static_cast<UINT32>(longEdge) * 2 <= maxDimension);
    if (canDouble) {
        attempts.push_back({ Prep::Doubled, L"2x" });
        if (dark) attempts.push_back({ Prep::DoubledInverted, L"2x inverted" });
    }

    // --- run them, keep whichever read the most --------------------------
    std::vector<OcrLine> observations;
    std::vector<OcrLine> candidate;
    const wchar_t* winner = L"none";
    size_t best = 0;
    bool anyPassRan = false;

    for (const Attempt& attempt : attempts) {
        // Built inside the loop and released at the end of it, so at most one
        // prepared copy is alive at a time. Building all four up front would
        // cost ten times the source in memory for a capture where the first
        // pass usually wins outright.
        std::unique_ptr<Bitmap> prepared;
        const Bitmap* target = source;
        switch (attempt.prep) {
        case Prep::Inverted:
            prepared = MakeInverted(*source);
            target = prepared.get();
            break;
        case Prep::Doubled:
            prepared = MakeUpscaled(*source, 2);
            target = prepared.get();
            break;
        case Prep::DoubledInverted: {
            std::unique_ptr<Bitmap> doubled = MakeUpscaled(*source, 2);
            if (doubled) prepared = MakeInverted(*doubled);
            target = prepared.get();
            break;
        }
        case Prep::AsCaptured:
            break;
        }
        if (!target) continue;

        if (!runPass(*target, candidate)) continue;   // the engine refused this one
        anyPassRan = true;

        const size_t count = characterCount(candidate);
        LOG_DEBUG(util::Format(L"ocr: pass '%s' read %zu chars on %zu lines",
                               attempt.name, count, candidate.size()));
        if (count > best) {
            best = count;
            observations = std::move(candidate);
            winner = attempt.name;
        }

        // A first pass that already read a substantial amount is the whole
        // picture; the variants exist for captures that came back nearly
        // empty. The bar is deliberately high — a dark screenshot full of
        // text should not pay for an inverted pass, but one that yielded a
        // single stray word should.
        if (attempt.prep == Prep::AsCaptured && count >= 200) break;
    }

    if (best == 0) {
        // Distinguish "read it, found nothing" from "never managed to read
        // it". The second is a real failure and the user should be told,
        // rather than shown the benign "no text found".
        if (!anyPassRan) {
            result.failure = L"Windows text recognition didn't complete. "
                             L"Try again, and see the log if it keeps happening.";
        }
        LOG_DEBUG(L"ocr: no pass found any text");
        return result;
    }
    LOG_DEBUG(util::Format(L"ocr: kept pass '%s' with %zu chars", winner, best));

    LOG_DEBUG(util::Format(L"ocr: %zu observations", observations.size()));
    BucketIntoRows(observations, result.lines);
    LOG_DEBUG(util::Format(L"ocr: %zu visual lines after row bucketing", result.lines.size()));
    return result;
}

} // namespace ocr
