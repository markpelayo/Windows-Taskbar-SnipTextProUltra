// Bitmap.h — a 32-bit BGRA top-down DIB section, and the conversions the rest
// of the program needs from it.
//
// One type for every image in the program: the frozen screen behind the
// region overlay, the capture handed to OCR, the backdrop in the annotation
// editor, and the flattened export. Peak memory is one of these — roughly
// 8 MB for a 1440p screen, 33 MB for 4K — so they are created late, released
// as soon as the work that needed them finishes, and never cached.

#pragma once

#include "framework.h"

class Bitmap {
public:
    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    Bitmap(Bitmap&&) noexcept;
    Bitmap& operator=(Bitmap&&) noexcept;
    ~Bitmap();

    static std::unique_ptr<Bitmap> Create(int width, int height);

    // Crops out a sub-rectangle. The rect is clamped to the source, so a
    // selection that ran off the edge of the screen still produces an image.
    std::unique_ptr<Bitmap> Crop(const RECT& region) const;

    bool IsValid() const { return bits_ != nullptr; }
    int  Width()   const { return width_; }
    int  Height()  const { return height_; }
    int  Stride()  const { return width_ * 4; }

    // Row-major, top-down, 4 bytes per pixel, B G R A.
    void*       Bits()       { return bits_; }
    const void* Bits() const { return bits_; }

    HBITMAP Handle() const { return handle_.get(); }

    // A memory DC with this bitmap selected. Created on first use and kept
    // for the bitmap's lifetime, because the editor repaints from it.
    HDC MemoryDC() const;

    // PNG bytes, for writing to disk and for the clipboard's PNG flavour.
    std::vector<BYTE> EncodePng() const;

    // Puts the image on the clipboard as CF_DIBV5 (which carries the alpha
    // channel) and as PNG, so both modern and older consumers find something
    // they understand.
    bool CopyToClipboard(HWND owner) const;

    // Fills with opaque black. A freshly created DIB section is already
    // zeroed, which is transparent black — fine for the editor's export
    // surface, wrong for anything that will be flattened onto the desktop.
    void FillOpaqueBlack();

    // Forces every alpha byte to 255. Screen captures come back from BitBlt
    // with an undefined alpha channel, and an image that reaches the
    // clipboard with zero alpha pastes as an invisible rectangle.
    void MakeOpaque();

private:
    void ReleaseMemoryDC() const;

    int          width_  = 0;
    int          height_ = 0;
    void*        bits_   = nullptr;
    ScopedBitmap handle_;
    mutable HDC  memoryDC_ = nullptr;
    mutable HGDIOBJ previousBitmap_ = nullptr;
};

namespace gdip {

// GDI+ is started once at launch and shut down once at exit. Drawing code
// asserts nothing about it; it is simply always available.
bool Startup();
void Shutdown();

} // namespace gdip
