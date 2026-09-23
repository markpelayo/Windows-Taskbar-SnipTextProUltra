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
            scaled->MakeOpaque();
            source = scaled.get();
            LOG_DEBUG(util::Format(L"ocr: downscaled %dx%d to %dx%d for the engine limit",
                                   image.Width(), image.Height(), width, height));
        }
    }

    ComPtr<ABIG::ISoftwareBitmap> softwareBitmap = MakeSoftwareBitmap(*source);
    if (!softwareBitmap) {
        result.failure = L"Couldn't hand the capture to the text recogniser.";
        return result;
    }

    ComPtr<ABIF::IAsyncOperation<ABIO::OcrResult*>> operation;
    if (FAILED(engine->RecognizeAsync(softwareBitmap.Get(), &operation)) || !operation) {
        result.failure = L"Text recognition refused the capture.";
        return result;
    }

    ComPtr<ABIO::IOcrResult> recognized;
    if (FAILED(AwaitOcrResult(operation.Get(), recognized.GetAddressOf())) || !recognized) {
        logging::Write(L"ocr: request FAILED");
        return result;   // empty lines, no failure message — treated as "no text"
    }

    ComPtr<ABIC::IVectorView<ABIO::OcrLine*>> engineLines;
    if (FAILED(recognized->get_Lines(&engineLines)) || !engineLines) return result;

    UINT32 lineCount = 0;
    engineLines->get_Size(&lineCount);
    if (lineCount == 0) {
        LOG_DEBUG(L"ocr: 0 observations");
        return result;
    }

    const double imageWidth  = static_cast<double>(source->Width());
    const double imageHeight = static_cast<double>(source->Height());

    std::vector<OcrLine> observations;
    observations.reserve(lineCount);

    for (UINT32 i = 0; i < lineCount; ++i) {
        ComPtr<ABIO::IOcrLine> engineLine;
        if (FAILED(engineLines->GetAt(i, &engineLine)) || !engineLine) continue;

        ComPtr<ABIC::IVectorView<ABIO::OcrWord*>> words;
        if (FAILED(engineLine->get_Words(&words)) || !words) continue;

        UINT32 wordCount = 0;
        words->get_Size(&wordCount);
        if (wordCount == 0) continue;

        // Geometry is unioned across the words in X and maxed in height, but
        // the vertical centre is the MEAN of the words' centres — a word with
        // a descender must not drag the whole line's centre down.
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
            // Counted here, not from the word count: the loop skips words
            // whose geometry could not be read, and dividing by the full
            // count would drag the line's centre toward the bottom of the
            // image and mis-bucket it into the wrong visual row.
            ++contributing;
        }

        if (first || text.empty() || contributing == 0) continue;

        OcrLine line;
        line.text   = std::move(text);
        line.minX   = minX;
        line.maxX   = maxX;
        line.height = maxHeight;
        line.midY   = midYSum / static_cast<double>(contributing);
        observations.push_back(std::move(line));
    }

    LOG_DEBUG(util::Format(L"ocr: %zu observations", observations.size()));
    BucketIntoRows(observations, result.lines);
    LOG_DEBUG(util::Format(L"ocr: %zu visual lines after row bucketing", result.lines.size()));
    return result;
}

} // namespace ocr
