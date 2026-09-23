// main.cpp — entry point.
//
// Nothing lives here but the DPI declaration, COM initialisation and the
// hand-off to App. Everything the program does is in App.

#include "App.h"
#include "framework.h"

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Per-Monitor-V2 DPI awareness, set before any window exists.
    //
    // This is not cosmetic. Without it Windows hands back virtualised,
    // scaled coordinates that do not match the pixels a capture actually
    // contains, so a region selected on a 150% display grabs the wrong
    // rectangle — and it works perfectly on a 100% monitor, which makes the
    // bug easy to miss. The manifest declares it too; this call is the
    // belt-and-braces for a build that somehow loses the manifest.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Apartment-threaded, because the shell dialogs this program opens
    // require it. The recorder's worker thread initialises its own
    // multithreaded apartment separately.
    const HRESULT comStatus = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool ownsCom = SUCCEEDED(comStatus);

    const bool ok = App::Shared().Run();

    if (ownsCom) ::CoUninitialize();
    return ok ? 0 : 1;
}
