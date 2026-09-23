#include "Bitmap.h"

#include "Log.h"
#include "Util.h"

#include <shlwapi.h>   // SHCreateMemStream

// gdiplustypes.h calls bare min() and max() inside its rectangle helpers, and
// this project builds with NOMINMAX so those macros do not exist. Putting the
// std versions into the Gdiplus namespace first satisfies the header without
// reintroducing the macros everywhere else.
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <objidl.h>
#include <gdiplus.h>

namespace {

ULONG_PTR g_gdiplusToken = 0;

bool PngEncoderClsid(CLSID* out) {
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return false;

    std::vector<BYTE> buffer(bytes);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return false;

    for (UINT i = 0; i < count; ++i) {
        if (::wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            *out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

} // namespace

namespace gdip {

bool Startup() {
    Gdiplus::GdiplusStartupInput input;
    return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

void Shutdown() {
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

} // namespace gdip

// ---------------------------------------------------------------------------

Bitmap::Bitmap(Bitmap&& other) noexcept
    : width_(other.width_),
      height_(other.height_),
      bits_(other.bits_),
      handle_(std::move(other.handle_)),
      memoryDC_(other.memoryDC_),
      previousBitmap_(other.previousBitmap_) {
    other.width_  = 0;
    other.height_ = 0;
    other.bits_   = nullptr;
    other.memoryDC_ = nullptr;
    other.previousBitmap_ = nullptr;
}

Bitmap& Bitmap::operator=(Bitmap&& other) noexcept {
    if (this != &other) {
        ReleaseMemoryDC();
        width_          = other.width_;
        height_         = other.height_;
        bits_           = other.bits_;
        handle_         = std::move(other.handle_);
        memoryDC_       = other.memoryDC_;
        previousBitmap_ = other.previousBitmap_;

        other.width_          = 0;
        other.height_         = 0;
        other.bits_           = nullptr;
        other.memoryDC_       = nullptr;
        other.previousBitmap_ = nullptr;
    }
    return *this;
}

Bitmap::~Bitmap() {
    ReleaseMemoryDC();
}

void Bitmap::ReleaseMemoryDC() const {
    if (memoryDC_) {
        // The bitmap must come out of the DC before either is destroyed, or
        // the DIB section stays alive for the process's lifetime.
        if (previousBitmap_) ::SelectObject(memoryDC_, previousBitmap_);
        ::DeleteDC(memoryDC_);
        memoryDC_ = nullptr;
        previousBitmap_ = nullptr;
    }
}

std::unique_ptr<Bitmap> Bitmap::Create(int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
    // Half a gigapixel, i.e. 2 GB at four bytes each. Far past any real
    // screen, and it keeps the byte arithmetic below well inside 64 bits.
    if (static_cast<long long>(width) * height > 512LL * 1024 * 1024) return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = width;
    info.bmiHeader.biHeight      = -height;   // negative: top-down rows
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    ScopedBitmap handle(::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!handle || !bits) return nullptr;

    auto bitmap = std::unique_ptr<Bitmap>(new Bitmap());
    bitmap->width_  = width;
    bitmap->height_ = height;
    bitmap->bits_   = bits;
    bitmap->handle_ = std::move(handle);
    return bitmap;
}

HDC Bitmap::MemoryDC() const {
    if (memoryDC_) return memoryDC_;
    if (!handle_) return nullptr;

    memoryDC_ = ::CreateCompatibleDC(nullptr);
    if (!memoryDC_) return nullptr;
    previousBitmap_ = ::SelectObject(memoryDC_, handle_.get());
    return memoryDC_;
}

std::unique_ptr<Bitmap> Bitmap::Crop(const RECT& region) const {
    if (!IsValid()) return nullptr;

    RECT clamped{};
    clamped.left   = (std::max)(0L, region.left);
    clamped.top    = (std::max)(0L, region.top);
    clamped.right  = (std::min)(static_cast<LONG>(width_),  region.right);
    clamped.bottom = (std::min)(static_cast<LONG>(height_), region.bottom);

    const int width  = util::RectWidth(clamped);
    const int height = util::RectHeight(clamped);
    if (width <= 0 || height <= 0) return nullptr;

    auto out = Create(width, height);
    if (!out) return nullptr;

    const BYTE* source = static_cast<const BYTE*>(bits_);
    BYTE*       target = static_cast<BYTE*>(out->Bits());
    for (int y = 0; y < height; ++y) {
        ::memcpy(target + static_cast<size_t>(y) * out->Stride(),
                 source + static_cast<size_t>(clamped.top + y) * Stride() + clamped.left * 4,
                 static_cast<size_t>(width) * 4);
    }
    return out;
}

void Bitmap::FillOpaqueBlack() {
    if (!IsValid()) return;
    BYTE* pixels = static_cast<BYTE*>(bits_);
    const size_t total = static_cast<size_t>(width_) * height_;
    for (size_t i = 0; i < total; ++i) {
        pixels[i * 4 + 0] = 0;
        pixels[i * 4 + 1] = 0;
        pixels[i * 4 + 2] = 0;
        pixels[i * 4 + 3] = 255;
    }
}

void Bitmap::MakeOpaque() {
    if (!IsValid()) return;
    BYTE* pixels = static_cast<BYTE*>(bits_);
    const size_t total = static_cast<size_t>(width_) * height_;
    for (size_t i = 0; i < total; ++i) pixels[i * 4 + 3] = 255;
}

std::vector<BYTE> Bitmap::EncodePng() const {
    std::vector<BYTE> out;
    if (!IsValid()) return out;

    CLSID encoder{};
    if (!PngEncoderClsid(&encoder)) {
        logging::Write(L"image: no PNG encoder available");
        return out;
    }

    // Wraps the DIB's memory rather than copying it. The Gdiplus::Bitmap must
    // not outlive this object, and it does not — it is destroyed below.
    Gdiplus::Bitmap source(width_, height_, Stride(), PixelFormat32bppARGB,
                           static_cast<BYTE*>(const_cast<void*>(bits_)));
    if (source.GetLastStatus() != Gdiplus::Ok) return out;

    IStream* rawStream = ::SHCreateMemStream(nullptr, 0);
    if (!rawStream) return out;

    if (source.Save(rawStream, &encoder, nullptr) == Gdiplus::Ok) {
        STATSTG stats{};
        if (SUCCEEDED(rawStream->Stat(&stats, STATFLAG_NONAME)) && stats.cbSize.QuadPart > 0 &&
            stats.cbSize.QuadPart < (1LL << 31)) {
            LARGE_INTEGER origin{};
            rawStream->Seek(origin, STREAM_SEEK_SET, nullptr);
            out.resize(static_cast<size_t>(stats.cbSize.QuadPart));
            ULONG read = 0;
            if (FAILED(rawStream->Read(out.data(), static_cast<ULONG>(out.size()), &read)) ||
                read != out.size()) {
                out.clear();
            }
        }
    }
    rawStream->Release();
    return out;
}

bool Bitmap::CopyToClipboard(HWND owner) const {
    if (!IsValid()) return false;

    // DIBV5 carries the alpha channel, which plain CF_DIB does not. Modern
    // consumers prefer PNG; both are offered, so neither kind of app has to
    // guess.
    const size_t pixelBytes = static_cast<size_t>(Stride()) * height_;
    const size_t totalBytes = sizeof(BITMAPV5HEADER) + pixelBytes;

    HGLOBAL dibHandle = ::GlobalAlloc(GMEM_MOVEABLE, totalBytes);
    if (!dibHandle) return false;

    {
        void* target = ::GlobalLock(dibHandle);
        if (!target) { ::GlobalFree(dibHandle); return false; }

        BITMAPV5HEADER header{};
        header.bV5Size        = sizeof(BITMAPV5HEADER);
        header.bV5Width       = width_;
        header.bV5Height      = -height_;
        header.bV5Planes      = 1;
        header.bV5BitCount    = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask     = 0x00FF0000;
        header.bV5GreenMask   = 0x0000FF00;
        header.bV5BlueMask    = 0x000000FF;
        header.bV5AlphaMask   = 0xFF000000;
        header.bV5CSType      = LCS_WINDOWS_COLOR_SPACE;
        header.bV5SizeImage   = static_cast<DWORD>(pixelBytes);

        ::memcpy(target, &header, sizeof(header));
        ::memcpy(static_cast<BYTE*>(target) + sizeof(header), bits_, pixelBytes);
        ::GlobalUnlock(dibHandle);
    }

    std::vector<BYTE> png = EncodePng();
    HGLOBAL pngHandle = nullptr;
    if (!png.empty()) {
        pngHandle = ::GlobalAlloc(GMEM_MOVEABLE, png.size());
        if (pngHandle) {
            void* target = ::GlobalLock(pngHandle);
            if (target) {
                ::memcpy(target, png.data(), png.size());
                ::GlobalUnlock(pngHandle);
            } else {
                ::GlobalFree(pngHandle);
                pngHandle = nullptr;
            }
        }
    }

    // OpenClipboard fails while another process holds the clipboard, which
    // happens routinely; a few retries turn a visible failure into a pause
    // nobody notices.
    bool opened = false;
    for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
        opened = ::OpenClipboard(owner) != FALSE;
        if (!opened) ::Sleep(20);
    }
    if (!opened) {
        ::GlobalFree(dibHandle);
        if (pngHandle) ::GlobalFree(pngHandle);
        return false;
    }

    ::EmptyClipboard();
    bool wrote = ::SetClipboardData(CF_DIBV5, dibHandle) != nullptr;
    // Once SetClipboardData succeeds the clipboard owns the handle; freeing
    // it here would be a double free.
    if (!wrote) ::GlobalFree(dibHandle);

    if (pngHandle) {
        static const UINT pngFormat = ::RegisterClipboardFormatW(L"PNG");
        if (!pngFormat || !::SetClipboardData(pngFormat, pngHandle)) ::GlobalFree(pngHandle);
    }

    ::CloseClipboard();
    return wrote;
}
