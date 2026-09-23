#include "Capture.h"

#include "Log.h"
#include "Util.h"

#include <mmsystem.h>

namespace capture {
namespace {

std::unique_ptr<Bitmap> GrabRect(const RECT& bounds) {
    const int width  = util::RectWidth(bounds);
    const int height = util::RectHeight(bounds);
    if (width <= 0 || height <= 0) return nullptr;

    auto image = Bitmap::Create(width, height);
    if (!image) {
        log::Write(util::Format(L"capture: couldn't allocate a %dx%d image", width, height));
        return nullptr;
    }

    HDC target = image->MemoryDC();
    if (!target) return nullptr;

    WindowDC screen(nullptr);   // the whole virtual desktop
    if (!screen) return nullptr;

    // CAPTUREBLT is what pulls in layered windows — without it a translucent
    // window over the region leaves a hole in the capture.
    if (!::BitBlt(target, 0, 0, width, height, screen.get(), bounds.left, bounds.top,
                  SRCCOPY | CAPTUREBLT)) {
        log::Write(L"capture: BitBlt failed");
        return nullptr;
    }

    // BitBlt leaves the alpha channel undefined. An image that reaches the
    // clipboard with zero alpha pastes as an invisible rectangle, and OCR
    // reads nothing from it.
    image->MakeOpaque();
    return image;
}

} // namespace

const wchar_t* ModeLabel(Mode mode) {
    return mode == Mode::Region ? L"region" : L"full screen";
}

std::unique_ptr<Bitmap> GrabMonitor(HMONITOR monitor) {
    return GrabRect(util::MonitorBounds(monitor));
}

std::unique_ptr<Bitmap> GrabVirtualDesktop(RECT* bounds) {
    RECT desktop{};
    desktop.left   = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    desktop.top    = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    desktop.right  = desktop.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    desktop.bottom = desktop.top  + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (bounds) *bounds = desktop;
    return GrabRect(desktop);
}

void PlayShutter() {
    // A named system sound rather than a bundled .wav: it respects the user's
    // sound scheme, and it keeps the executable a single self-contained file.
    if (!::PlaySoundW(L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT)) {
        ::MessageBeep(MB_OK);
    }
}

} // namespace capture
