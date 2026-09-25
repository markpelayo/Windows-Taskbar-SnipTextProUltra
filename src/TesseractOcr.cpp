#include "TesseractOcr.h"

#include "Log.h"
#include "Util.h"
#include "resource.h"

#ifdef SNIPTEXT_WITH_TESSERACT

#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#include <leptonica/allheaders.h>

#include <atomic>
#include <memory>

namespace tesseract_ocr {
namespace {

// --- the embedded model ----------------------------------------------------
//
// Held as a pointer into the executable's own resource block, so it costs no
// heap and no file. FindResource/LoadResource on a resource that is already
// mapped is effectively free, and the memory is released with the module.
struct EmbeddedModel {
    const char* data = nullptr;
    int         size = 0;
};

EmbeddedModel LoadEmbeddedModel() {
    EmbeddedModel model;
    HMODULE module = ::GetModuleHandleW(nullptr);

    HRSRC found = ::FindResourceW(module, MAKEINTRESOURCEW(IDR_TESSDATA_ENG), RT_RCDATA);
    if (!found) {
        logging::Write(L"tesseract: the trained model is missing from this build");
        return model;
    }
    HGLOBAL loaded = ::LoadResource(module, found);
    if (!loaded) return model;

    model.data = static_cast<const char*>(::LockResource(loaded));
    model.size = static_cast<int>(::SizeofResource(module, found));
    if (!model.data || model.size <= 0) {
        model.data = nullptr;
        model.size = 0;
    }
    return model;
}

// --- one recognition -------------------------------------------------------

// Owns the API for exactly one capture. End() before destruction is what
// releases the model and the internal image buffers; relying on the
// destructor alone leaves them until process exit on some Tesseract versions.
class ScopedApi {
public:
    ScopedApi() : api_(std::make_unique<tesseract::TessBaseAPI>()) {}
    ScopedApi(const ScopedApi&) = delete;
    ScopedApi& operator=(const ScopedApi&) = delete;
    ~ScopedApi() { if (api_) api_->End(); }

    tesseract::TessBaseAPI* get() const { return api_.get(); }
    tesseract::TessBaseAPI* operator->() const { return api_.get(); }

private:
    std::unique_ptr<tesseract::TessBaseAPI> api_;
};

// Leptonica's PIX, freed on every exit path.
struct PixDeleter {
    void operator()(PIX* pix) const { if (pix) pixDestroy(&pix); }
};
using ScopedPix = std::unique_ptr<PIX, PixDeleter>;

// Our BGRA top-down DIB into a Leptonica 32-bit PIX. Leptonica packs RGBA
// big-endian within each word, which is not the same order, so this converts
// rather than aliasing the buffer.
ScopedPix ToPix(const Bitmap& image) {
    if (!image.IsValid()) return nullptr;

    ScopedPix pix(pixCreate(image.Width(), image.Height(), 32));
    if (!pix) return nullptr;

    const BYTE* source = static_cast<const BYTE*>(image.Bits());
    l_uint32* target   = pixGetData(pix.get());
    const int words    = pixGetWpl(pix.get());

    for (int y = 0; y < image.Height(); ++y) {
        l_uint32* row = target + static_cast<size_t>(y) * words;
        const BYTE* from = source + static_cast<size_t>(y) * image.Stride();
        for (int x = 0; x < image.Width(); ++x) {
            const l_uint8 b = from[x * 4 + 0];
            const l_uint8 g = from[x * 4 + 1];
            const l_uint8 r = from[x * 4 + 2];
            composeRGBPixel(r, g, b, &row[x]);
        }
    }
    // Screen text is already crisp; 300 dpi is the resolution Tesseract's
    // models were trained around and stops it second-guessing the scale.
    pixSetResolution(pix.get(), 300, 300);
    return pix;
}

std::atomic<int> g_availability{ -1 };   // -1 unknown, 0 no, 1 yes

} // namespace

bool IsCompiledIn() { return true; }

bool IsAvailable() {
    const int cached = g_availability.load(std::memory_order_relaxed);
    if (cached >= 0) return cached == 1;

    const EmbeddedModel model = LoadEmbeddedModel();
    const bool available = model.data != nullptr;
    g_availability.store(available ? 1 : 0, std::memory_order_relaxed);
    return available;
}

Result Recognize(const Bitmap& image) {
    Result result;
    if (!image.IsValid()) return result;

    const EmbeddedModel model = LoadEmbeddedModel();
    if (!model.data) {
        result.failure = L"The bundled text-recognition model is missing from this build.";
        return result;
    }

    ScopedApi api;

    // THE point of this engine, and it has to be done HERE rather than with
    // SetVariable afterwards.
    //
    // `load_system_dawg` and `load_freq_dawg` are init-time variables: they
    // are read inside Dict::Load(), which runs during Init(). Setting them
    // after Init() is accepted, does nothing, and leaves the lexicon fully
    // on — so the engine would go on rewriting low-confidence characters
    // into whatever makes a real word, which is the exact behaviour this
    // whole file exists to escape.
    const std::vector<std::string> variableNames  = { "load_system_dawg",
                                                      "load_freq_dawg" };
    const std::vector<std::string> variableValues = { "0", "0" };

    // Init from memory rather than from a tessdata directory: there is no
    // directory, and there is not going to be one.
    //
    // OEM_LSTM_ONLY because the legacy engine is not in tessdata_fast and
    // asking for it would fail. The trailing nullptr is the FileReader, which
    // has no default argument on this overload.
    if (api->Init(model.data, model.size, "eng", tesseract::OEM_LSTM_ONLY,
                  nullptr, 0, &variableNames, &variableValues, false, nullptr) != 0) {
        logging::Write(L"tesseract: the engine refused to initialise");
        result.failure = L"The fallback text recogniser couldn't start.";
        return result;
    }

    // A screenshot is not a page. Sparse layout analysis finds text scattered
    // anywhere rather than assuming columns and paragraphs.
    api->SetPageSegMode(tesseract::PSM_SPARSE_TEXT);

    ScopedPix pix = ToPix(image);
    if (!pix) {
        result.failure = L"Couldn't hand the capture to the fallback recogniser.";
        return result;
    }
    api->SetImage(pix.get());

    if (api->Recognize(nullptr) != 0) {
        logging::Write(L"tesseract: recognition failed");
        return result;   // empty, treated as "no text"
    }

    std::unique_ptr<tesseract::ResultIterator> it(api->GetIterator());
    if (!it) return result;

    const double imageWidth  = static_cast<double>(image.Width());
    const double imageHeight = static_cast<double>(image.Height());
    constexpr auto level = tesseract::RIL_TEXTLINE;

    do {
        // Confidence is per line here. Anything under 30 is noise — a JPEG
        // artefact read as a hyphen — and letting it through would be worse
        // than the gap it fills.
        if (it->Empty(level)) continue;
        const float confidence = it->Confidence(level);
        if (confidence < 30.0f) continue;

        std::unique_ptr<char, void(*)(void*)> raw(it->GetUTF8Text(level),
                                                  [](void* p) { delete[] static_cast<char*>(p); });
        if (!raw) continue;

        std::wstring text = util::FromUtf8(raw.get());
        // Tesseract terminates lines with a newline of its own.
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
        if (text.empty()) continue;

        int left = 0, top = 0, right = 0, bottom = 0;
        if (!it->BoundingBox(level, &left, &top, &right, &bottom)) continue;

        OcrLine line;
        line.text   = std::move(text);
        line.minX   = left / imageWidth;
        line.maxX   = right / imageWidth;
        line.height = (bottom - top) / imageHeight;
        // Flip Y: Tesseract reports a top-left origin, the normaliser assumes
        // bottom-left.
        line.midY   = 1.0 - ((top + bottom) / 2.0) / imageHeight;
        result.lines.push_back(std::move(line));
    } while (it->Next(level));

    LOG_DEBUG(util::Format(L"tesseract: %zu lines", result.lines.size()));
    return result;
}

} // namespace tesseract_ocr

#else   // SNIPTEXT_WITH_TESSERACT

// The dependency-free build. Everything above compiles to these three
// answers, so no caller needs an #ifdef of its own.
namespace tesseract_ocr {

bool IsCompiledIn() { return false; }
bool IsAvailable()  { return false; }

Result Recognize(const Bitmap&) { return Result{}; }

} // namespace tesseract_ocr

#endif  // SNIPTEXT_WITH_TESSERACT
