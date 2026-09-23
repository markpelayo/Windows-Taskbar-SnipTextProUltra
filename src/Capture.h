// Capture.h — getting pixels off the screen.
//
// The process is declared Per-Monitor-V2 DPI aware in the manifest, so every
// coordinate here is already in physical pixels on every monitor. Without
// that declaration Windows hands back virtualised, scaled coordinates that do
// not match the captured surface, and a capture on a 150% display silently
// grabs the wrong region — the Windows equivalent of the Retina crop bug the
// macOS version had to fix.

#pragma once

#include "Bitmap.h"

namespace capture {

enum class Mode { Region, FullScreen };

const wchar_t* ModeLabel(Mode mode);

// Grabs one monitor, in that monitor's own physical pixels.
std::unique_ptr<Bitmap> GrabMonitor(HMONITOR monitor);

// Grabs every monitor as one image. `bounds` receives the virtual-desktop
// rectangle it covers, so a point in desktop coordinates maps to a pixel by
// subtracting bounds.left / bounds.top.
std::unique_ptr<Bitmap> GrabVirtualDesktop(RECT* bounds);

// Plays the capture confirmation. There is no notification banner anywhere in
// this program, which also means no notification permission prompt.
void PlayShutter();

} // namespace capture
