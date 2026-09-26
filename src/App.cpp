#include "App.h"

#include "Bitmap.h"
#include "Clipboard.h"
#include "EditorSettings.h"
#include "Hotkeys.h"
#include "Log.h"
#include "MediaFolder.h"
#include "Ocr.h"
#include "RecordingIndicator.h"
#include "RegionOverlay.h"
#include "ScreenRecorder.h"
#include "Settings.h"
#include "TesseractOcr.h"
#include "TextNormalizer.h"
#include "Toast.h"
#include "Util.h"
#include "VideoSettings.h"
#include "resource.h"

#include <commctrl.h>

namespace {

constexpr const wchar_t* kWindowClass = L"SnipTextMain";
constexpr const wchar_t* kMutexName   = L"Local\\SnipTextProUltraSingleInstance";

constexpr UINT WM_TRAY_ICON     = WM_APP + 10;
constexpr UINT WM_OCR_FINISHED  = WM_APP + 11;
constexpr UINT WM_REAP_EDITORS  = WM_APP + 12;

constexpr UINT_PTR kRecordingTimer = 1;
constexpr UINT_PTR kSetupTimer     = 2;
constexpr UINT     kTrayIconId     = 1;

// Menu command identifiers.
enum : int {
    ID_SHOT_REGION = 1001, ID_SHOT_FULL, ID_SHOT_SHOW,
    ID_TEXT_REGION, ID_TEXT_FULL, ID_TEXT_COPYLAST, ID_TEXT_SHOW,
    ID_REC_REGION, ID_REC_FULL, ID_REC_STOP, ID_REC_SHOW,
    // ID_SET_JOINWRAPPED was here; Text Layout replaced the checkbox with the
    // two rows below. The slot is kept so ID_SET_AUTOSAVE keeps its number.
    ID_SET_LAYOUT_RETIRED, ID_SET_AUTOSAVE,
    ID_FOLDER_SHOT_CHOOSE = 1020, ID_FOLDER_SHOT_RESET,
    ID_FOLDER_TEXT_CHOOSE, ID_FOLDER_TEXT_RESET,
    ID_FOLDER_VIDEO_CHOOSE, ID_FOLDER_VIDEO_RESET,
    ID_LAYOUT_REBUILD = 1030, ID_LAYOUT_LINES,
    ID_AFTER_EDITOR = 1034, ID_AFTER_CLIPBOARD,
    ID_VID_CURSOR = 1040, ID_VID_CLICKS, ID_VID_HEVC,
    ID_SANITIZE = 1050, ID_QUIT, ID_ABOUT,
    ID_STARTUP_OFF = 1060, ID_STARTUP_ON,
    ID_SHORTCUT_RESET = 1070,
    // 1075 was ID_SHUTTER_BUILTIN. The slot is kept rather than closed up so
    // that CUSTOM and PREVIEW keep the numbers they have always had.
    ID_SHUTTER_OFF = 1074, ID_SHUTTER_RETIRED_1075, ID_SHUTTER_CUSTOM, ID_SHUTTER_PREVIEW,
    ID_ENGINE_AUTO = 1090, ID_ENGINE_WINDOWS, ID_ENGINE_TESSERACT,
    ID_SHORTCUT_BASE  = 1080,   // + index into hotkeys::kAllActions
    ID_FPS_BASE      = 1100,   // + index into kFrameRateChoices
    ID_QUALITY_BASE  = 1110,   // + index
    ID_AUDIO_NONE    = 1120,
    ID_AUDIO_BASE    = 1121,   // + index into AvailableMicrophones()
    ID_DELAY_BASE    = 1200,   // + index into kStartupDelayChoices
    ID_TONE_BASE     = 1210,   // + index into capture::shutter, 5 of them
    ID_COMPRESSION_BASE = 1220, // + index into video::Compression
};

const int kStartupDelayChoices[6] = { 5, 10, 15, 20, 30, 60 };

constexpr const wchar_t* kRepositoryUrl =
    L"https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra";

UINT RelaunchMessage() {
    static const UINT message = ::RegisterWindowMessageW(L"SnipTextProUltra.ShowMenu");
    return message;
}

// Explorer broadcasts this after it restarts. Without handling it, an
// Explorer crash would take the tray icon with it permanently while the app
// went on believing the icon was there.
UINT TaskbarCreatedMessage() {
    static const UINT message = ::RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}

void AppendHeader(HMENU menu, const wchar_t* title) {
    if (!menu) return;
    // Windows has no section-header menu item, so this is a disabled entry.
    // MFS_DISABLED rather than MFS_GRAYED keeps it unselectable without the
    // heavier greyed-out styling that would read as "broken".
    MENUITEMINFOW item{};
    item.cbSize     = sizeof(item);
    item.fMask      = MIIM_STRING | MIIM_STATE | MIIM_ID;
    item.fState     = MFS_DISABLED;
    item.wID        = 0;
    item.dwTypeData = const_cast<wchar_t*>(title);
    ::InsertMenuItemW(menu, ::GetMenuItemCount(menu), TRUE, &item);
}

void AppendCommand(HMENU menu, int id, const std::wstring& title,
                   bool enabled = true, bool checked = false) {
    if (!menu) return;
    UINT flags = MF_STRING;
    if (!enabled) flags |= MF_GRAYED;
    if (checked)  flags |= MF_CHECKED;
    ::AppendMenuW(menu, flags, static_cast<UINT_PTR>(id), title.c_str());
}

void AppendSeparator(HMENU menu) {
    if (menu) ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
}

void AppendSubmenu(HMENU parent, HMENU child, const std::wstring& title,
                   bool checked = false, bool enabled = true) {
    // A null child would produce an MF_POPUP item that cannot be opened —
    // worse than the row simply not being there.
    if (!parent || !child) return;
    UINT flags = MF_POPUP;
    if (checked)  flags |= MF_CHECKED;
    if (!enabled) flags |= MF_GRAYED;
    ::AppendMenuW(parent, flags, reinterpret_cast<UINT_PTR>(child), title.c_str());
}

// The first row: which program this is, which version, and whose it is.
//
// A Win32 menu item cannot be a hyperlink — there is no such thing — so this
// is an ordinary enabled command that opens the repository in the browser.
// That is the closest honest equivalent, and it means the row does something
// rather than just sitting there greyed out.
void AppendTitleRow(HMENU menu) {
    if (!menu) return;
    // The app name rather than the repository name. The repository's
    // "Windows-Taskbar-" prefix says which platform it targets, which is
    // information the program running on that platform does not need — and it
    // made this row the widest in the menu, which set the width of every row
    // beneath it.
    const std::wstring title = L"SnipTextProUltra  ·  v" SNIPTEXT_VERSION_DISPLAY
                               L"  ·  by markpelayo";
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ID_ABOUT), title.c_str());
}

ocr::Engine CurrentEngine() {
    const std::wstring stored = settings::GetString(settings::key::kOcrEngine, L"auto");
    if (stored == L"windows")   return ocr::Engine::WindowsOnly;
    if (stored == L"tesseract") return ocr::Engine::TesseractOnly;
    return ocr::Engine::Auto;
}

// The shortcut column, read live rather than hard-coded, so rebinding one
// updates the menu the next time it opens.
// Includes the tab, so an unbound action produces no accelerator column at
// all rather than a label ending in a bare tab — which Win32 still reserves
// the column's width for.
std::wstring ShortcutLabel(hotkeys::Action action) {
    const hotkeys::Binding binding = hotkeys::Current(action);
    return binding.IsBound() ? L"\t" + hotkeys::Describe(binding) : std::wstring();
}

// "Screenshots (12)" or "Videos — none yet". The count is part of the label
// because the alternative is opening a folder to find out it is empty.
std::wstring SavedItemTitle(const wchar_t* title, int count) {
    return count > 0 ? util::Format(L"%s (%d)", title, count)
                     : std::wstring(title) + L" — none yet";
}

// Collapses newlines, trims, then truncates by user-perceived characters.
// Counting UTF-16 units would cut a surrogate pair in half.
std::wstring Preview(const std::wstring& text, size_t limit) {
    std::wstring oneLine = text;
    for (wchar_t& c : oneLine) {
        if (c == L'\r' || c == L'\n') c = L' ';
    }
    size_t first = oneLine.find_first_not_of(L" \t");
    size_t last  = oneLine.find_last_not_of(L" \t");
    if (first == std::wstring::npos) return std::wstring();
    oneLine = oneLine.substr(first, last - first + 1);

    std::vector<char32_t> points = util::ToCodePoints(oneLine);
    if (points.size() <= limit) return oneLine;
    points.resize(limit);
    return util::FromCodePoints(points) + L"…";
}

// The application icon, at the tray's preferred small size. Loaded once and
// never destroyed: it lives for the life of the process, and a static holding
// an HICON would run its destructor at CRT exit, after GDI teardown.
HICON IdleTrayIcon() {
    static HICON icon = static_cast<HICON>(::LoadImageW(
        ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SNIPTEXT), IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    return icon;
}

ScopedIcon MakeStopIcon() {
    // Generated rather than loaded from a resource, so there is one fewer
    // thing in the binary that has to be kept in step with the drawing code.
    constexpr int size = 32;
    auto image = Bitmap::Create(size, size);
    // Deliberately no MemoryDC() call: the pixels are written straight
    // through Bits(), and a bitmap that is currently selected into a DC
    // cannot be handed to CreateIconIndirect.
    if (!image) return ScopedIcon();

    BYTE* pixels = static_cast<BYTE*>(image->Bits());
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            BYTE* pixel = pixels + (static_cast<size_t>(y) * size + x) * 4;
            const bool inside = x >= 7 && x < 25 && y >= 7 && y < 25;
            pixel[0] = 35;   // B
            pixel[1] = 17;   // G
            pixel[2] = 232;  // R  — the Windows 11 system red
            pixel[3] = inside ? 255 : 0;
        }
    }

    // Explicitly zeroed. CreateBitmap with a null buffer leaves the bits
    // undefined, and CreateIconIndirect reads the AND mask on some shell
    // paths even for a 32-bit colour bitmap — which shows up as garbage
    // holes in the square.
    static const BYTE zeroMask[size * 4] = {};   // 1bpp, DWORD-aligned rows
    ScopedBitmap mask(::CreateBitmap(size, size, 1, 1, zeroMask));
    if (!mask) return ScopedIcon();

    ICONINFO info{};
    info.fIcon    = TRUE;
    info.hbmMask  = mask.get();
    info.hbmColor = image->Handle();
    return ScopedIcon(::CreateIconIndirect(&info));
}

// Same lifetime reasoning as IdleTrayIcon.
HICON RecordingTrayIcon() {
    static HICON icon = MakeStopIcon().release();
    return icon ? icon : IdleTrayIcon();
}

} // namespace

// ---------------------------------------------------------------------------

struct App::OcrOutcome {
    std::unique_ptr<Bitmap> image;
    capture::Mode           mode = capture::Mode::Region;
    bool                    keepLineBreaks = false;
    ocr::Engine             engine = ocr::Engine::Auto;
    std::wstring            text;
    size_t                  lineCount = 0;
    std::wstring            failure;
};

App& App::Shared() {
    static App app;
    return app;
}

// --- lifecycle -------------------------------------------------------------

bool App::Run() {
    // Single instance. A second launch is how the pinned taskbar icon is
    // "clicked", so it must reach the running process rather than start a
    // rival one that would fail to register the same hotkeys.
    ScopedHandle instanceMutex(::CreateMutexW(nullptr, TRUE, kMutexName));
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = ::FindWindowW(kWindowClass, nullptr)) {
            ::PostMessageW(existing, RelaunchMessage(), 0, 0);
        }
        return true;
    }

    // Defaults to on during the 1.x shakedown; see the note in Log.h. A
    // machine that has explicitly set debugMode still wins either way.
    logging::SetVerbose(settings::GetBool(settings::key::kDebugMode,
                                          logging::kVerboseByDefault));
    logging::StartSession(L"SnipText launched, log at " + logging::FilePath());
    WriteStartupDiagnostics();

    if (!gdip::Startup()) {
        logging::Write(L"launch: GDI+ failed to start");
        return false;
    }
    if (!CreateHiddenWindow()) {
        gdip::Shutdown();
        return false;
    }

    ScreenRecorder::Shared().Initialise();
    ScreenRecorder::Shared().SetOnStateChange([this] { OnRecordingStateChanged(); });
    ScreenRecorder::Shared().SetOnFinish(
        [this](const std::wstring& path, const std::wstring& failure) {
            OnRecordingFinished(path, failure);
        });

    // The startup delay applies only when Windows started the app after a
    // boot, never to a launch the user asked for.
    const int delay = settings::GetInt(settings::key::kStartupDelay, 0);
    if (delay > 0 && LaunchedAtLogin()) {
        logging::Write(util::Format(L"startup: login launch, holding setup for %ds", delay));
        ::SetTimer(hwnd_, kSetupTimer, static_cast<UINT>(delay) * 1000, nullptr);
    } else {
        SetUpAfterStartupDelay();
        // A deliberate launch means the user wants the menu, now. The pinned
        // icon has no other way to say so.
        ShowMenu();
    }

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    ScreenRecorder::Shared().Shutdown();
    RecordingIndicator::Shared().Hide();
    toast::Destroy();
    gdip::Shutdown();
    logging::Shutdown();
    return true;
}

// TEMPORARY, part of the 1.x shakedown — see the note in Log.h.
//
// Everything here is the context a bug report needs and nobody remembers to
// include: which Windows build, how many monitors and at what scaling, and
// whether an OCR language is installed at all. Written once per launch.
void App::WriteStartupDiagnostics() {
    // --- build ---
    logging::Write(util::Format(L"env: SnipText %s (%s, built %S %S)",
                                SNIPTEXT_VERSION_DISPLAY,
#ifdef _WIN64
                                L"x64",
#else
                                L"x86",
#endif
                                __DATE__, __TIME__));

    // --- Windows version ---
    // Through RtlGetVersion, not GetVersionEx: the documented API lies to
    // applications whose manifest does not list the running OS, and reports
    // Windows 8 forever. OSVERSIONINFOEXW is layout-compatible with the
    // RTL_OSVERSIONINFOEXW that RtlGetVersion actually takes, and is declared
    // in winnt.h rather than the internal headers.
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    bool gotVersion = false;
    if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")));
        gotVersion = rtlGetVersion != nullptr && rtlGetVersion(&version) == 0;
    }
    if (gotVersion) {
        // Windows 11 still reports major version 10; the build number is the
        // only thing that actually distinguishes it.
        const wchar_t* name = (version.dwBuildNumber >= 22000) ? L"Windows 11"
                            : (version.dwMajorVersion >= 10)   ? L"Windows 10"
                                                               : L"Windows (pre-10)";
        logging::Write(util::Format(L"env: %s %lu.%lu build %lu",
                                    name, version.dwMajorVersion, version.dwMinorVersion,
                                    version.dwBuildNumber));
    } else {
        logging::Write(L"env: couldn't read the Windows version");
    }

    // --- displays ---
    // The one thing most likely to be different on the machine where a
    // capture lands in the wrong place.
    int monitorIndex = 0;
    ::EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL {
            int* index = reinterpret_cast<int*>(parameter);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!::GetMonitorInfoW(monitor, &info)) return TRUE;

            UINT dpiX = 96, dpiY = 96;
            ::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            // Only the horizontal figure is reported; the two are equal on
            // every shipping display, and GetDpiForMonitor will not accept a
            // null for the second.
            (void)dpiY;

            logging::Write(util::Format(
                L"env: display %d %s %ldx%ld at (%ld,%ld), %u dpi (%d%%)",
                (*index)++,
                (info.dwFlags & MONITORINFOF_PRIMARY) ? L"[primary]" : L"         ",
                info.rcMonitor.right - info.rcMonitor.left,
                info.rcMonitor.bottom - info.rcMonitor.top,
                info.rcMonitor.left, info.rcMonitor.top,
                dpiX, static_cast<int>(dpiX * 100 / 96)));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitorIndex));

    logging::Write(util::Format(L"env: virtual desktop %dx%d at (%d,%d), DPI awareness %s",
                                ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
                                ::GetSystemMetrics(SM_CYVIRTUALSCREEN),
                                ::GetSystemMetrics(SM_XVIRTUALSCREEN),
                                ::GetSystemMetrics(SM_YVIRTUALSCREEN),
                                ::AreDpiAwarenessContextsEqual(
                                    ::GetThreadDpiAwarenessContext(),
                                    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
                                    ? L"PerMonitorV2 (correct)"
                                    : L"NOT PerMonitorV2 — captures will be misplaced"));

    // --- OCR ---
    // Answered once here so "Screenshot to Text did nothing" is diagnosable
    // from the log alone. The result is cached, so the first real capture
    // pays nothing for it.
    logging::Write(ocr::IsAvailable()
                       ? L"env: OCR engine available"
                       : L"env: NO OCR language installed — Screenshot to Text will fail");

    // --- where things are ---
    logging::Write(L"env: screenshots  -> " + MediaFolder::Screenshots().Directory());
    logging::Write(L"env: text images  -> " + MediaFolder::TextImages().Directory());
    logging::Write(L"env: videos       -> " + MediaFolder::Videos().Directory());
}

bool App::CreateHiddenWindow() {
    WNDCLASSEXW description{};
    description.cbSize        = sizeof(description);
    description.lpfnWndProc   = &App::WindowProc;
    description.hInstance     = ::GetModuleHandleW(nullptr);
    description.lpszClassName = kWindowClass;
    description.hIcon         = ::LoadIconW(::GetModuleHandleW(nullptr),
                                            MAKEINTRESOURCEW(IDI_SNIPTEXT));
    ::RegisterClassExW(&description);

    // A real top-level window rather than a message-only one: RegisterHotKey
    // wants a window that belongs to the thread, and a zero-size
    // WS_EX_TOOLWINDOW never appears in the taskbar or in Alt-Tab.
    hwnd_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"SnipTextProUltra", WS_POPUP,
                              0, 0, 0, 0, nullptr, nullptr,
                              ::GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        logging::Write(L"launch: couldn't create the main window");
        return false;
    }
    return true;
}

void App::SetUpAfterStartupDelay() {
    // Part of "the app is now up and usable", so it waits out the startup
    // delay alongside the hotkeys rather than appearing before them.
    ShowTrayIcon();
    RegisterHotkeys();
}

void App::RegisterHotkeys() {
    // The bindings themselves, their defaults and their persistence all live
    // in hotkeys::, so the menu, the registration and the rebinding dialog
    // cannot disagree about what is bound to what.
    hotkeys::Register(hwnd_);
}

bool App::LaunchedAtLogin() {
    if (!settings::IsRunAtStartupEnabled()) {
        logging::Write(L"startup: not registered to run at startup, launching now");
        return false;
    }
    // Startup entries fire moments after the desktop appears, so a launch
    // inside the first two minutes of uptime is a startup launch and anything
    // later is the user. The bias is deliberate: a missing delay is
    // invisible, while a wrong one looks like a broken app.
    const ULONGLONG uptimeMs = ::GetTickCount64();
    const bool isLogin = uptimeMs < 120000;
    logging::Write(util::Format(L"startup: uptime %llus — treating as a %s launch",
                   uptimeMs / 1000, isLogin ? L"login" : L"manual"));
    return isLogin;
}

// --- message routing -------------------------------------------------------

LRESULT CALLBACK App::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    return self->HandleMessage(message, wParam, lParam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == RelaunchMessage()) {
        ShowMenu();
        return 0;
    }
    if (message == TaskbarCreatedMessage()) {
        // The old icon went with Explorer, so the flag is stale.
        trayIconVisible_ = false;
        ShowTrayIcon();
        UpdateRecordingIndicator();
        return 0;
    }

    switch (message) {
    case WM_HOTKEY:
        // The hotkey id IS the action, so a rebinding changes which keys
        // arrive here and nothing else.
        switch (static_cast<hotkeys::Action>(wParam)) {
        case hotkeys::Action::ScreenshotRegion:
            Screenshot(capture::Mode::Region); return 0;
        case hotkeys::Action::ScreenshotFullScreen:
            Screenshot(capture::Mode::FullScreen); return 0;
        case hotkeys::Action::TextRegion:
            ScreenshotToText(capture::Mode::Region); return 0;
        case hotkeys::Action::TextFullScreen:
            ScreenshotToText(capture::Mode::FullScreen); return 0;
        // Both recording hotkeys are toggles. Once the overlay is gone the
        // only feedback is the indicator, and a toggle is what you reach for
        // then — so either one stops a recording, whichever started it.
        case hotkeys::Action::RecordRegion:
            ToggleRecording(true); return 0;
        case hotkeys::Action::RecordFullScreen:
            ToggleRecording(false); return 0;
        }
        return 0;

    case WM_TRAY_ICON:
        // Either button opens the menu. A left-click that did something
        // different from a right-click would be a trap, and while recording
        // the menu's first row is already "Stop Recording (MM:SS)" — with the
        // Stop pill on screen as the one-click route.
        if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_RBUTTONUP) {
            ShowMenu();
        }
        return 0;

    case WM_TIMER:
        if (wParam == kRecordingTimer) {
            recordingBlinkOn_ = !recordingBlinkOn_;
            UpdateRecordingIndicator();
        } else if (wParam == kSetupTimer) {
            ::KillTimer(hwnd_, kSetupTimer);
            SetUpAfterStartupDelay();
        }
        return 0;

    case WM_DEVICECHANGE:
        // The microphone list is cached because enumerating audio endpoints
        // costs real milliseconds and the menu reads it on every open. This
        // is the only thing that invalidates it.
        video::InvalidateMicrophoneCache();
        return TRUE;

    case WM_OCR_FINISHED:
        OnOcrFinished(reinterpret_cast<OcrOutcome*>(lParam));
        return 0;

    case WM_REAP_EDITORS:
        ReapClosedEditors();
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;

    case WM_QUERYENDSESSION:
        // Finish the file before saying yes: the MP4 index is written when
        // the recording stops, and Windows will not wait afterwards.
        ScreenRecorder::Shared().FinishBeforeQuit();
        return TRUE;

    case WM_ENDSESSION:
        // Must return zero once handled, unlike WM_QUERYENDSESSION.
        ScreenRecorder::Shared().FinishBeforeQuit();
        return 0;

    case WM_DESTROY:
        hotkeys::Unregister(hwnd_);
        HideTrayIcon();
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd_, message, wParam, lParam);
}

// --- the menu --------------------------------------------------------------

HMENU App::BuildMenu() {
    // Rebuilt from scratch on every open. Refreshing at the end of whichever
    // action changed something was the original approach and went wrong in a
    // specific way: a screenshot wrote a file without refreshing the menu, so
    // the saved count only moved when some unrelated action happened to
    // rebuild it. Rebuilding on display makes every count correct by
    // construction.
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return nullptr;

    // Enumerated once and shared by the "Show Saved" items and the Sanitize
    // enablement, so the dialog's numbers and the menu's always agree.
    const int screenshotCount = MediaFolder::Screenshots().Count();
    const int textImageCount  = MediaFolder::TextImages().Count();
    const int videoCount      = MediaFolder::Videos().Count();
    const bool recording      = ScreenRecorder::Shared().IsRecording();

    AppendTitleRow(menu);
    AppendSeparator(menu);

    // No headers over the capture sections any more: the command names now
    // carry the section, so a header would repeat the row beneath it. The shortcut column
    // is filled from the live bindings rather than hard-coded, so a rebound
    // shortcut shows up here immediately.
    AppendCommand(menu, ID_SHOT_REGION,
                  L"Screenshot a Region…" + ShortcutLabel(hotkeys::Action::ScreenshotRegion));
    AppendCommand(menu, ID_SHOT_FULL,
                  L"Screenshot Full Screen"
                      + ShortcutLabel(hotkeys::Action::ScreenshotFullScreen));
    AppendSeparator(menu);

    AppendCommand(menu, ID_TEXT_REGION,
                  L"ScreenshotToText a Region…" + ShortcutLabel(hotkeys::Action::TextRegion));
    AppendCommand(menu, ID_TEXT_FULL,
                  L"ScreenshotToText Full Screen"
                      + ShortcutLabel(hotkeys::Action::TextFullScreen));
    if (lastText_.empty()) {
        AppendCommand(menu, ID_TEXT_COPYLAST, L"No text captured yet", false);
    } else {
        // Capped at 14 characters because this is the one label whose width
        // varies with the user's data; a generous cap would make the menu
        // change width every time it was used.
        AppendCommand(menu, ID_TEXT_COPYLAST,
                      L"Copy: “" + Preview(lastText_, 14) + L"”");
    }
    AppendSeparator(menu);

    if (recording) {
        AppendCommand(menu, ID_REC_STOP,
                      L"Stop Recording (" + ScreenRecorder::Shared().ElapsedText() + L")");
    } else {
        AppendCommand(menu, ID_REC_REGION,
                      L"Screen Record a Region…"
                          + ShortcutLabel(hotkeys::Action::RecordRegion));
        AppendCommand(menu, ID_REC_FULL,
                      L"Screen Record Full Screen"
                          + ShortcutLabel(hotkeys::Action::RecordFullScreen));
    }
    AppendSeparator(menu);

    // No "Settings" header: the rows below are visibly settings, and the
    // separator above already marks the break.
    //
    // Shortcuts comes first, because it is the one setting that changes what
    // the rows above this point say.
    {
        HMENU shortcuts = ::CreatePopupMenu();
        if (shortcuts) {
            for (int i = 0; i < hotkeys::kActionCount; ++i) {
                const hotkeys::Action action = hotkeys::kAllActions[i];
                AppendCommand(shortcuts, ID_SHORTCUT_BASE + i,
                              std::wstring(hotkeys::ActionTitle(action)) + L"\t"
                                  + hotkeys::Describe(hotkeys::Current(action)));
            }
            AppendSeparator(shortcuts);
            AppendCommand(shortcuts, ID_SHORTCUT_RESET, L"Reset to Defaults",
                          !hotkeys::IsDefault());
        }
        AppendSubmenu(menu, shortcuts, L"Change Keyboard Shortcut");
    }

    // A submenu rather than a checkbox, and the third name this setting has
    // had. "Keep Line Breaks" was wrong because it described the unchecked
    // state; "Join Wrapped Lines" was wrong because it described only half of
    // what the checked state does, and on a screenshot with no wrapped lines
    // in it — a chat list, a table, anything already truncated with an
    // ellipsis — that half does nothing at all, so the only visible effect was
    // the blank lines between blocks, which the name never mentioned.
    //
    // The pattern in both failures is the checkbox: it can only name one
    // state, so the other one is always inferred, and a wrong inference is
    // invisible until someone compares two captures side by side. Naming both
    // states costs one row and ends the guessing.
    {
        const bool rebuild = settings::GetBool(settings::key::kJoinWrappedLines, true);
        HMENU layout = ::CreatePopupMenu();
        if (layout) {
            AppendCommand(layout, ID_LAYOUT_REBUILD, L"Rebuild Paragraphs", true, rebuild);
            AppendCommand(layout, ID_LAYOUT_LINES, L"Keep Every Line Separate", true, !rebuild);
            AppendSeparator(layout);
            AppendHeader(layout, L"Rebuild rejoins sentences that wrapped,");
            AppendHeader(layout, L"and puts a blank line between blocks.");
        }
        AppendSubmenu(menu, layout, L"Text Layout");
    }

    // Only worth showing when there is a choice to make. A build without the
    // fallback engine has one recogniser, and a row that offers one option is
    // noise.
    if (tesseract_ocr::IsCompiledIn()) {
        const ocr::Engine engine = CurrentEngine();
        HMENU engines = ::CreatePopupMenu();
        if (engines) {
            AppendCommand(engines, ID_ENGINE_AUTO,
                          L"Auto \u2014 Windows, then the fallback", true,
                          engine == ocr::Engine::Auto);
            AppendCommand(engines, ID_ENGINE_WINDOWS,
                          L"Windows only \u2014 fastest", true,
                          engine == ocr::Engine::WindowsOnly);
            AppendCommand(engines, ID_ENGINE_TESSERACT,
                          L"Fallback only \u2014 reads symbols and codes", true,
                          engine == ocr::Engine::TesseractOnly);
        }
        AppendSubmenu(menu, engines, L"Text Recognition");
    }
    {
        const bool shutterOn = settings::GetBool(settings::key::kShutterSound, true);
        const std::wstring customPath = settings::GetString(settings::key::kShutterSoundPath);

        const int tone = capture::shutter::CurrentTone();

        HMENU shutter = ::CreatePopupMenu();
        if (shutter) {
            AppendCommand(shutter, ID_SHUTTER_OFF, L"Off", true, !shutterOn);
            AppendSeparator(shutter);
            // The five built-ins are listed flat rather than behind another
            // submenu: picking a sound means comparing them, and comparing
            // them means being able to run down the list. Choosing one plays
            // it, so the list auditions itself.
            for (int i = 0; i < capture::shutter::kToneCount; ++i) {
                AppendCommand(shutter, ID_TONE_BASE + i,
                              capture::shutter::ToneName(i), true,
                              shutterOn && customPath.empty() && tone == i);
            }
            AppendSeparator(shutter);
            AppendCommand(shutter, ID_SHUTTER_CUSTOM,
                          customPath.empty()
                              ? std::wstring(L"Custom Sound\u2026")
                              : L"Custom: " + util::LastPathComponent(customPath) + L"\u2026",
                          true, shutterOn && !customPath.empty());
            AppendSeparator(shutter);
            AppendCommand(shutter, ID_SHUTTER_PREVIEW, L"Preview");
        }
        AppendSubmenu(menu, shutter, L"Shutter Sound", shutterOn);
    }

    // What a Screenshot command does once it has the pixels. Two rows rather
    // than a checkbox for the same reason as Text Layout above: the choice is
    // between two named behaviours, not between a behaviour and the absence of
    // one, and "unchecked" would have had to carry "opens the editor" by
    // implication.
    //
    // ScreenshotToText is unaffected — it never opened the editor — so the
    // title says "a Screenshot" and means the two commands that did.
    {
        const bool skipEditor = settings::GetBool(settings::key::kSkipEditor, false);
        HMENU after = ::CreatePopupMenu();
        if (after) {
            AppendCommand(after, ID_AFTER_EDITOR, L"Open the Editor", true, !skipEditor);
            AppendCommand(after, ID_AFTER_CLIPBOARD, L"Copy to Clipboard and Close",
                          true, skipEditor);
            AppendSeparator(after);
            AppendHeader(after, L"Auto-Save still applies either way.");
        }
        AppendSubmenu(menu, after, L"After a Screenshot");
    }

    AppendCommand(menu, ID_SET_AUTOSAVE, L"Auto-Save Images to Local Machine", true,
                  settings::GetBool(settings::key::kSaveCaptures, false));

    // --- where the three capture commands write ---
    // Collected under one row. Three top-level folder rows, each able to grow
    // a ": FolderName" suffix, were the second-widest thing in the menu after
    // the title row.
    struct FolderEntry { MediaFolder& folder; const wchar_t* title; int chooseId; int resetId; };
    const FolderEntry folderEntries[] = {
        { MediaFolder::Screenshots(), L"Screenshots",
          ID_FOLDER_SHOT_CHOOSE, ID_FOLDER_SHOT_RESET },
        { MediaFolder::TextImages(), L"ScreenshotToText Images",
          ID_FOLDER_TEXT_CHOOSE, ID_FOLDER_TEXT_RESET },
        { MediaFolder::Videos(), L"Videos",
          ID_FOLDER_VIDEO_CHOOSE, ID_FOLDER_VIDEO_RESET },
    };

    if (HMENU locations = ::CreatePopupMenu()) {
        for (const FolderEntry& entry : folderEntries) {
            HMENU submenu = ::CreatePopupMenu();
            if (!submenu) continue;
            ::AppendMenuW(submenu, MF_STRING | MF_GRAYED, 0,
                          entry.folder.DisplayPath().c_str());
            AppendSeparator(submenu);
            AppendCommand(submenu, entry.chooseId, L"Choose Folder…");
            AppendCommand(submenu, entry.resetId, L"Reset to Default",
                          !entry.folder.IsUsingDefaultDirectory());

            // No "Default" suffix while at the default location: the folder
            // name only appears once it carries information.
            std::wstring title = entry.folder.IsUsingDefaultDirectory()
                ? std::wstring(entry.title)
                : std::wstring(entry.title) + L": "
                  + util::LastPathComponent(entry.folder.Directory());
            AppendSubmenu(locations, submenu, title);
        }
        AppendSubmenu(menu, locations, L"Save Locations");
    }

    // --- everything the three capture commands have produced ---
    // One row rather than three scattered through the capture sections: they
    // are the same kind of thing, and grouping them keeps each section to its
    // commands alone.
    {
        HMENU saved = ::CreatePopupMenu();
        if (saved) {
            AppendCommand(saved, ID_SHOT_SHOW,
                          SavedItemTitle(L"Screenshots", screenshotCount),
                          screenshotCount > 0);
            AppendCommand(saved, ID_TEXT_SHOW,
                          SavedItemTitle(L"ScreenshotToText Images", textImageCount),
                          textImageCount > 0);
            AppendCommand(saved, ID_REC_SHOW,
                          SavedItemTitle(L"Videos", videoCount),
                          videoCount > 0);
        }
        // Disabled outright when all three folders are empty, so the parent
        // row behaves the way the individual rows used to: it tells you there
        // is nothing there rather than opening to three dead entries.
        const bool anySaved = (screenshotCount + textImageCount + videoCount) > 0;
        AppendSubmenu(menu, saved, L"Show Saved Files", false, anySaved);
    }

    // --- Screen Recording Settings ---
    {
        HMENU videoMenu = ::CreatePopupMenu();
        if (videoMenu) {
            HMENU frameRates = ::CreatePopupMenu();
            for (int i = 0; i < 4; ++i) {
                const int fps = video::kFrameRateChoices[i];
                AppendCommand(frameRates, ID_FPS_BASE + i,
                              util::Format(L"%d frames per second", fps),
                              true, fps == video::FrameRate());
            }
            AppendSubmenu(videoMenu, frameRates,
                          util::Format(L"Frame Rate: %d fps", video::FrameRate()));

            HMENU qualities = ::CreatePopupMenu();
            for (int i = 0; i < 3; ++i) {
                const auto quality = static_cast<video::Quality>(i);
                AppendCommand(qualities, ID_QUALITY_BASE + i, video::QualityTitle(quality),
                              true, quality == video::CurrentQuality());
            }
            AppendSubmenu(videoMenu, qualities,
                          std::wstring(L"Quality: ")
                              + video::QualityShortTitle(video::CurrentQuality()));

            // File size, which is a different axis from Quality: Quality
            // scales the picture down, this changes how many bits are spent on
            // whatever size that is. Both end up smaller; only one of them
            // makes the video blurry when you zoom in.
            HMENU compression = ::CreatePopupMenu();
            for (int i = 0; i < 3; ++i) {
                const auto value = static_cast<video::Compression>(i);
                AppendCommand(compression, ID_COMPRESSION_BASE + i,
                              video::CompressionTitle(value), true,
                              value == video::CurrentCompression());
            }
            AppendSeparator(compression);
            AppendCommand(compression, ID_VID_HEVC,
                          L"Use H.265 When Available", true, video::UsesHevc());
            AppendHeader(compression, L"H.265 halves the size again,");
            AppendHeader(compression, L"but older players can't open it.");
            AppendSubmenu(videoMenu, compression,
                          std::wstring(L"File Size: ")
                              + video::CompressionShortTitle(video::CurrentCompression()));

            AppendSeparator(videoMenu);
            AppendCommand(videoMenu, ID_VID_CURSOR, L"Capture Mouse Cursor", true,
                          video::CapturesCursor());
            AppendCommand(videoMenu, ID_VID_CLICKS, L"Capture Mouse Clicks", true,
                          video::CapturesClicks());
            AppendSeparator(videoMenu);

            HMENU audio = ::CreatePopupMenu();
            const std::wstring selectedId = video::AudioDeviceId();
            AppendCommand(audio, ID_AUDIO_NONE, L"Do Not Record Audio", true,
                          selectedId.empty());
            const std::vector<video::Microphone>& microphones = video::AvailableMicrophones();
            if (!microphones.empty()) {
                AppendHeader(audio, L"Microphone:");
                for (size_t i = 0; i < microphones.size(); ++i) {
                    AppendCommand(audio, ID_AUDIO_BASE + static_cast<int>(i),
                                  microphones[i].name, true, microphones[i].id == selectedId);
                }
            }

            std::wstring audioTitle = L"Audio: Do Not Record";
            if (!selectedId.empty()) {
                const video::Microphone* selected = video::SelectedMicrophone();
                audioTitle = L"Audio: " + (selected ? selected->name : std::wstring(L"Unavailable"));
            }
            AppendSubmenu(videoMenu, audio, audioTitle);

            AppendSubmenu(menu, videoMenu, L"Screen Recording Settings");
        }
    }

    // --- Sanitize ---
    const bool atDefaults =
        MediaFolder::Screenshots().IsUsingDefaultDirectory()
        && MediaFolder::TextImages().IsUsingDefaultDirectory()
        && MediaFolder::Videos().IsUsingDefaultDirectory()
        &&  settings::GetBool(settings::key::kJoinWrappedLines, true)
        &&  settings::GetBool(settings::key::kShutterSound, true)
        && !settings::GetBool(settings::key::kSaveCaptures, false)
        && !settings::GetBool(settings::key::kSkipEditor, false)
        &&  settings::GetInt(settings::key::kStartupDelay, 0) == 0
        && !settings::IsRunAtStartupEnabled()
        &&  video::IsDefault()
        &&  editor_settings::IsDefault()
        &&  hotkeys::IsDefault()
        &&  settings::GetString(settings::key::kShutterSoundPath).empty()
        &&  settings::GetInt(settings::key::kShutterTone, 0) == 0
        &&  settings::GetString(settings::key::kOcrEngine).empty()
        &&  lastText_.empty();
    const int totalFiles = screenshotCount + textImageCount + videoCount;
    AppendCommand(menu, ID_SANITIZE, L"Sanitize and Restore Default…",
                  totalFiles > 0 || !atDefaults);
    AppendSeparator(menu);

    // --- Startup ---
    // No header: the row below already begins with "Run at Startup", so a
    // header would be the same word twice in a row.
    {
        HMENU startup = ::CreatePopupMenu();
        const bool enabled = settings::IsRunAtStartupEnabled();
        const int  delay   = settings::GetInt(settings::key::kStartupDelay, 0);
        if (startup) {
            AppendCommand(startup, ID_STARTUP_OFF, L"Off", true, !enabled);
            AppendCommand(startup, ID_STARTUP_ON,  L"On",  true, enabled && delay <= 0);
            AppendHeader(startup, L"Delay for:");
            for (int i = 0; i < 6; ++i) {
                const int seconds = kStartupDelayChoices[i];
                AppendCommand(startup, ID_DELAY_BASE + i, util::Format(L"%d s", seconds),
                              true, enabled && delay == seconds);
            }

            std::wstring title = L"Run at Startup: Off";
            if (enabled) {
                title = (delay <= 0) ? L"Run at Startup: On"
                                     : util::Format(L"Run at Startup: %d s", delay);
            }
            // Checked whenever startup is enabled, at any delay — so the
            // state is readable from the parent row without opening the
            // submenu to go looking for it.
            //
            // Only attached if the submenu was actually created; an MF_POPUP
            // item with a null handle is a menu item that cannot be used.
            AppendSubmenu(menu, startup, title, enabled);
        }
    }
    AppendSeparator(menu);

    AppendCommand(menu, ID_QUIT, L"Quit SnipTextProUltra");
    return menu;
}

void App::ShowMenu() {
    // Never stack a menu on top of a crosshair.
    if (isCapturing_ || RegionOverlay::IsShowing()) return;

    ScopedMenu menu(BuildMenu());
    if (!menu) return;

    POINT cursor{};
    ::GetCursorPos(&cursor);

    // Anchored just above the taskbar rather than at the raw cursor, so the
    // menu rises out of the icon the way a menubar menu drops out of one.
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = (std::min)((std::max)(static_cast<int>(cursor.x),
                                        static_cast<int>(work.left) + 8),
                             static_cast<int>(work.right) - 8);
    const int y = static_cast<int>(work.bottom);

    // The foreground dance: without it, a menu raised by a process that is
    // not in the foreground refuses to dismiss when the user clicks away.
    ::SetForegroundWindow(hwnd_);

    const int command = ::TrackPopupMenuEx(menu.get(),
                                           TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN
                                               | TPM_RIGHTBUTTON,
                                           x, y, hwnd_, nullptr);
    ::PostMessageW(hwnd_, WM_NULL, 0, 0);

    // Destroyed before the command runs. Several commands are modal — a
    // crosshair, a folder picker, a task dialog — and holding a menu handle
    // across one of those serves no purpose.
    menu.reset();

    if (command > 0) OnCommand(command);
}

void App::OnCommand(int command) {
    if (command >= ID_FPS_BASE && command < ID_FPS_BASE + 4) {
        video::SetFrameRate(video::kFrameRateChoices[command - ID_FPS_BASE]);
        return;
    }
    if (command >= ID_QUALITY_BASE && command < ID_QUALITY_BASE + 3) {
        video::SetQuality(static_cast<video::Quality>(command - ID_QUALITY_BASE));
        return;
    }
    if (command >= ID_COMPRESSION_BASE && command < ID_COMPRESSION_BASE + 3) {
        video::SetCompression(static_cast<video::Compression>(command - ID_COMPRESSION_BASE));
        return;
    }
    if (command >= ID_TONE_BASE && command < ID_TONE_BASE + capture::shutter::kToneCount) {
        const int tone = command - ID_TONE_BASE;
        settings::SetInt(settings::key::kShutterTone, tone);
        // Choosing a built-in tone is also what switches back off a custom
        // sound, and back on if the shutter was muted — otherwise picking a
        // sound from the list would appear to do nothing. The custom file
        // itself is left alone on disk.
        settings::Remove(settings::key::kShutterSoundPath);
        settings::SetBool(settings::key::kShutterSound, true);
        // Play the tone directly rather than the configured shutter: this is
        // the audition, and it should be the sound that was just clicked.
        capture::shutter::PlayTone(tone);
        return;
    }
    if (command >= ID_DELAY_BASE && command < ID_DELAY_BASE + 6) {
        ApplyStartup(true, kStartupDelayChoices[command - ID_DELAY_BASE]);
        return;
    }
    if (command >= ID_SHORTCUT_BASE && command < ID_SHORTCUT_BASE + hotkeys::kActionCount) {
        const hotkeys::Action action = hotkeys::kAllActions[command - ID_SHORTCUT_BASE];
        hotkeys::Binding captured;
        if (hotkeys::CaptureBinding(hwnd_, action, &captured)) {
            // Two actions on one combination means the second registration
            // fails and that action is silently keyboard-unreachable. Taking
            // the binding away from the previous owner makes the outcome
            // match what the user just asked for.
            if (captured.IsBound()) {
                for (hotkeys::Action other : hotkeys::kAllActions) {
                    if (other == action) continue;
                    if (hotkeys::Current(other) == captured) {
                        hotkeys::Set(other, hotkeys::Binding{});
                        logging::Write(util::Format(
                            L"hotkey: %s gave up %s to %s",
                            hotkeys::ActionTitle(other),
                            hotkeys::Describe(captured).c_str(),
                            hotkeys::ActionTitle(action)));
                    }
                }
            }
            hotkeys::Set(action, captured);
            // CaptureBinding puts the old registrations back before it
            // returns, so the new one needs a fresh pass to take effect.
            hotkeys::Unregister(hwnd_);
            hotkeys::Register(hwnd_);
            logging::Write(util::Format(L"hotkey: %s is now %s",
                                        hotkeys::ActionTitle(action),
                                        hotkeys::Describe(captured).c_str()));
        }
        return;
    }
    if (command >= ID_AUDIO_BASE && command < ID_DELAY_BASE) {
        const size_t index = static_cast<size_t>(command - ID_AUDIO_BASE);
        const std::vector<video::Microphone>& microphones = video::AvailableMicrophones();
        if (index < microphones.size()) video::SetAudioDeviceId(microphones[index].id);
        return;
    }

    switch (command) {
    case ID_SHOT_REGION:  Screenshot(capture::Mode::Region); return;
    case ID_SHOT_FULL:    Screenshot(capture::Mode::FullScreen); return;
    case ID_SHOT_SHOW:    MediaFolder::Screenshots().Reveal(); return;

    case ID_TEXT_REGION:  ScreenshotToText(capture::Mode::Region); return;
    case ID_TEXT_FULL:    ScreenshotToText(capture::Mode::FullScreen); return;
    case ID_TEXT_SHOW:    MediaFolder::TextImages().Reveal(); return;
    case ID_TEXT_COPYLAST:
        if (!lastText_.empty() && clipboard::CopyText(hwnd_, lastText_)) toast::Show(L"copied");
        return;

    case ID_REC_REGION:   BeginRecording(true); return;
    case ID_REC_FULL:     BeginRecording(false); return;
    case ID_REC_STOP:     ScreenRecorder::Shared().Stop(); return;
    case ID_REC_SHOW:     MediaFolder::Videos().Reveal(); return;

    // Set, not toggled. Two rows each naming a state means clicking the one
    // that is already ticked has to be a no-op — with a toggle it would turn
    // the setting off, which is the opposite of what the row says.
    //
    // Rebuild is the default, so choosing it *removes* the value rather than
    // writing true: absent and default have to stay the same thing, or
    // Sanitize stops being able to tell whether anything was ever changed.
    case ID_LAYOUT_REBUILD:
        settings::Remove(settings::key::kJoinWrappedLines);
        return;
    case ID_LAYOUT_LINES:
        settings::SetBool(settings::key::kJoinWrappedLines, false);
        return;

    // Set rather than toggled, and the default row removes the value — the
    // same two rules as Text Layout, for the same two reasons.
    case ID_AFTER_EDITOR:
        settings::Remove(settings::key::kSkipEditor);
        return;
    case ID_AFTER_CLIPBOARD:
        settings::SetBool(settings::key::kSkipEditor, true);
        return;
    case ID_ENGINE_AUTO:
        settings::Remove(settings::key::kOcrEngine);   // absent means the default
        return;
    case ID_ENGINE_WINDOWS:
        settings::SetString(settings::key::kOcrEngine, L"windows");
        return;
    case ID_ENGINE_TESSERACT:
        settings::SetString(settings::key::kOcrEngine, L"tesseract");
        return;

    case ID_SHUTTER_OFF:
        settings::SetBool(settings::key::kShutterSound, false);
        return;

    // ID_SHUTTER_BUILTIN is gone: the five named tones replaced the single
    // "Built-in Shutter" row, and each of them does what it did.
    //
    // Worth recording why it is not simply left in place as a sixth way to get
    // the same result. Its body had picked up a stray line —
    //
    //     settings::Remove(settings::key::kOcrEngine);
    //
    // — misindented, from a bad paste, with no business being there. Choosing
    // a shutter sound silently reset the text-recognition engine to Auto.
    // Nothing reported it because both settings are invisible until you go
    // looking, and the menu redraws from the registry, so the check mark moved
    // and looked deliberate. Deleting the case removes the bug with it.

    case ID_SHUTTER_CUSTOM:
        ChooseShutterSound();
        return;

    case ID_SHUTTER_PREVIEW:
        // Plays whatever is configured even when the shutter is switched
        // off, so you can hear a sound before deciding to turn it on.
        capture::PreviewShutter();
        return;
    case ID_SET_AUTOSAVE: {
        const bool on = !settings::GetBool(settings::key::kSaveCaptures, false);
        settings::SetBool(settings::key::kSaveCaptures, on);
        logging::Write(on ? L"save images on" : L"save images off");
        return;
    }

    case ID_FOLDER_SHOT_CHOOSE:  ChooseFolder(MediaFolder::Screenshots()); return;
    case ID_FOLDER_TEXT_CHOOSE:  ChooseFolder(MediaFolder::TextImages()); return;
    case ID_FOLDER_VIDEO_CHOOSE: ChooseFolder(MediaFolder::Videos()); return;
    case ID_FOLDER_SHOT_RESET:
        MediaFolder::Screenshots().SetDirectory(L"");
        logging::Write(L"screenshots: folder reset to " + MediaFolder::Screenshots().Directory());
        return;
    case ID_FOLDER_TEXT_RESET:
        MediaFolder::TextImages().SetDirectory(L"");
        logging::Write(L"text images: folder reset to " + MediaFolder::TextImages().Directory());
        return;
    case ID_FOLDER_VIDEO_RESET:
        MediaFolder::Videos().SetDirectory(L"");
        logging::Write(L"videos: folder reset to " + MediaFolder::Videos().Directory());
        return;

    case ID_VID_CURSOR: video::SetCapturesCursor(!video::CapturesCursor()); return;
    case ID_VID_CLICKS: video::SetCapturesClicks(!video::CapturesClicks()); return;
    case ID_VID_HEVC:   video::SetUsesHevc(!video::UsesHevc()); return;
    case ID_AUDIO_NONE: video::SetAudioDeviceId(L""); return;

    case ID_STARTUP_OFF: ApplyStartup(false, 0); return;
    case ID_STARTUP_ON:  ApplyStartup(true, 0); return;

    case ID_SHORTCUT_RESET:
        hotkeys::ResetAll();
        hotkeys::Unregister(hwnd_);
        hotkeys::Register(hwnd_);
        logging::Write(L"hotkey: all shortcuts reset to defaults");
        return;

    case ID_ABOUT:
        ::ShellExecuteW(nullptr, L"open", kRepositoryUrl, nullptr, nullptr, SW_SHOWNORMAL);
        return;

    case ID_SANITIZE: Sanitize(); return;

    case ID_QUIT:
        ScreenRecorder::Shared().FinishBeforeQuit();
        ::DestroyWindow(hwnd_);
        return;
    }
}

// --- capture pipelines -----------------------------------------------------

std::unique_ptr<Bitmap> App::AcquireImage(capture::Mode mode) {
    if (mode == capture::Mode::FullScreen) {
        return capture::GrabMonitor(util::MonitorUnderCursor());
    }

    RegionOverlay overlay;
    const RegionOverlay::Selection selection = overlay.Run(RegionOverlay::Style::Instant);
    if (!selection.confirmed || !overlay.FrozenDesktop()) return nullptr;
    return overlay.FrozenDesktop()->Crop(selection.bounds);
}

void App::Screenshot(capture::Mode mode) {
    if (isCapturing_ || RegionOverlay::IsShowing()) {
        LOG_DEBUG(std::wstring(L"screenshot: ignored ") + capture::ModeLabel(mode)
                  + L", a capture or overlay is already up");
        return;
    }
    isCapturing_ = true;

    std::unique_ptr<Bitmap> image = AcquireImage(mode);

    // Cleared before anything that can put a dialog on screen. A deferred
    // reset would keep the app locked out of new captures for as long as the
    // dialog stayed up.
    isCapturing_ = false;

    if (!image) {
        LOG_DEBUG(L"screenshot: cancelled");
        return;   // a cancel does nothing at all: no sound, no message, no file
    }

    if (settings::GetBool(settings::key::kShutterSound, true)) capture::PlayShutter();

    // Auto-save is orthogonal to what happens next: with both on, the shot is
    // written to disk AND put on the clipboard, and nothing opens.
    if (settings::GetBool(settings::key::kSaveCaptures, false)) {
        std::vector<BYTE> png = image->EncodePng();
        if (!png.empty()) MediaFolder::Screenshots().SaveBytes(png.data(), png.size());
    }

    if (settings::GetBool(settings::key::kSkipEditor, false)) {
        const int width  = image->Width();
        const int height = image->Height();

        // The same call the editor's Ctrl+C makes, so a quick capture and an
        // unedited one put byte-identical data on the clipboard.
        const bool copied = image->CopyToClipboard(hwnd_);
        image.reset();   // the editor is not going to take it

        if (copied) {
            logging::Write(util::Format(L"screenshot: copied %dx%d to the clipboard (%s)",
                                        width, height, capture::ModeLabel(mode)));
            toast::Show(util::Format(L"copied %d × %d", width, height));
        } else {
            // Another process can hold the clipboard open, and then the shot
            // exists nowhere the user can reach unless auto-save happened to
            // catch it. Silence here would look exactly like success.
            logging::Write(L"screenshot: the clipboard refused the image");
            toast::Show(L"couldn't copy — the clipboard is busy");
        }
        return;
    }

    logging::Write(util::Format(L"screenshot: editor opened %dx%d (%s)",
                   image->Width(), image->Height(), capture::ModeLabel(mode)));
    OpenEditor(std::move(image));
}

void App::ScreenshotToText(capture::Mode mode) {
    if (isCapturing_ || RegionOverlay::IsShowing()) {
        LOG_DEBUG(std::wstring(L"pipeline: ignored ") + capture::ModeLabel(mode)
                  + L", a capture or overlay is already up");
        return;
    }
    isCapturing_ = true;

    // The normaliser's flag is still "keep the breaks", which is the inverse
    // of the setting. Inverted once, here, so nothing downstream has to hold
    // both senses in its head.
    // The registry key still reads joinWrappedLines. Renaming it would make
    // every existing install silently revert to the default, and the key is
    // not the part anyone sees.
    const bool rebuildParagraphs = settings::GetBool(settings::key::kJoinWrappedLines, true);
    const bool keepLineBreaks    = !rebuildParagraphs;
    LOG_DEBUG(util::Format(L"pipeline: start mode=%s layout=%s",
                           capture::ModeLabel(mode),
                           rebuildParagraphs ? L"rebuild" : L"lines"));

    std::unique_ptr<Bitmap> image = AcquireImage(mode);
    if (!image) {
        isCapturing_ = false;
        LOG_DEBUG(L"pipeline: cancelled, nothing copied");
        return;
    }

    if (settings::GetBool(settings::key::kShutterSound, true)) capture::PlayShutter();
    if (settings::GetBool(settings::key::kSaveCaptures, false)) {
        std::vector<BYTE> png = image->EncodePng();
        if (!png.empty()) MediaFolder::TextImages().SaveBytes(png.data(), png.size());
    }

    // OCR runs off the UI thread: recognition on a full-screen capture takes
    // long enough that doing it here would visibly freeze whatever the user
    // was working in.
    auto* outcome = new OcrOutcome();
    outcome->image          = std::move(image);
    outcome->mode           = mode;
    outcome->keepLineBreaks = keepLineBreaks;
    // Resolved here rather than on the worker: the setting lives in the
    // registry, and reading it from two threads is needless.
    outcome->engine         = CurrentEngine();

    ScopedHandle thread(::CreateThread(nullptr, 0, &App::OcrThread, outcome, 0, nullptr));
    if (!thread) {
        delete outcome;
        isCapturing_ = false;
        ReportFailure(L"Couldn't start text recognition.");
    }
    // `isCapturing_` stays true until the result lands, so a second text-capture
    // during recognition is ignored rather than racing to the clipboard.
}

DWORD WINAPI App::OcrThread(void* parameter) {
    auto* outcome = static_cast<OcrOutcome*>(parameter);

    ocr::Result recognised = ocr::Recognize(*outcome->image, outcome->engine);
    outcome->failure   = recognised.failure;
    outcome->lineCount = recognised.lines.size();
    outcome->text      = text::Normalize(recognised.lines, outcome->keepLineBreaks);

    // The image is released here rather than on the UI thread: it is the
    // largest allocation in the program and there is no reason to keep it
    // alive across a thread hop.
    outcome->image.reset();

    // If the window has gone — the app quit while recognition was running —
    // the post fails and nobody will ever take ownership of the outcome.
    if (!::PostMessageW(App::Shared().hwnd_, WM_OCR_FINISHED, 0,
                        reinterpret_cast<LPARAM>(outcome))) {
        delete outcome;
    }
    return 0;
}

void App::OnOcrFinished(OcrOutcome* raw) {
    std::unique_ptr<OcrOutcome> outcome(raw);
    isCapturing_ = false;
    if (!outcome) return;

    if (!outcome->failure.empty()) {
        logging::Write(L"pipeline: failure — " + outcome->failure);
        ReportFailure(outcome->failure);
        return;
    }

    if (outcome->text.empty()) {
        logging::Write(L"capture produced no readable text");
        toast::Show(L"no text found");
        return;
    }

    LOG_DEBUG(util::Format(L"pipeline: %zu lines → %zu chars",
                           outcome->lineCount, outcome->text.size()));

    const bool copied = clipboard::CopyText(hwnd_, outcome->text);
    // Recorded regardless of whether the clipboard write landed, so "Copy
    // last" can retry it.
    lastText_ = outcome->text;

    toast::Show(copied ? util::Format(L"%zu chars copied", outcome->text.size())
                       : std::wstring(L"copy failed"));
    logging::Write(util::Format(L"copied %zu chars from %zu lines (%s)",
                   outcome->text.size(), outcome->lineCount,
                   capture::ModeLabel(outcome->mode)));
}

// --- recording -------------------------------------------------------------

void App::ToggleRecording(bool region) {
    if (ScreenRecorder::Shared().IsRecording()) {
        ScreenRecorder::Shared().Stop();
        return;
    }
    BeginRecording(region);
}

void App::BeginRecording(bool region) {
    if (ScreenRecorder::Shared().IsRecording()) return;
    if (isCapturing_ || RegionOverlay::IsShowing()) {
        LOG_DEBUG(L"recorder: ignored, a capture or overlay is already up");
        return;
    }

    RECT target{};
    if (region) {
        RegionOverlay overlay;
        const RegionOverlay::Selection selection = overlay.Run(RegionOverlay::Style::Adjustable);
        if (!selection.confirmed) return;

        // The overlay's rectangle is relative to the frozen desktop image;
        // the recorder needs virtual-desktop coordinates.
        const RECT desktop = overlay.DesktopBounds();
        target = selection.bounds;
        ::OffsetRect(&target, desktop.left, desktop.top);

        // The recorder substitutes the monitor under the cursor for an empty
        // region. Resolving that here too keeps recordingRegion_ — and so the
        // green frame — describing what is actually being recorded.
        if (util::RectWidth(target) <= 0 || util::RectHeight(target) <= 0) {
            target = util::MonitorBounds(util::MonitorUnderCursor());
        }

        // The overlay's window is already hidden at this point, and the
        // overlay object is still alive, so IsShowing() still reports true —
        // which is what stops a second overlay appearing in the first frames
        // of the recording about to start.
        recordingRegion_ = target;
        const std::wstring failure = ScreenRecorder::Shared().Start(target);
        if (!failure.empty()) ReportFailure(failure);
        return;
    }

    target = util::MonitorBounds(util::MonitorUnderCursor());
    recordingRegion_ = target;
    const std::wstring failure = ScreenRecorder::Shared().Start(target);
    if (!failure.empty()) ReportFailure(failure);
}

void App::OnRecordingStateChanged() {
    ::KillTimer(hwnd_, kRecordingTimer);

    if (ScreenRecorder::Shared().IsRecording()) {
        recordingBlinkOn_ = true;
        // One timer drives the elapsed text, the blink and the pill, so the
        // three can never drift out of step.
        ::SetTimer(hwnd_, kRecordingTimer, 1000, nullptr);
        toast::SetSuppressed(true);
        // The green frame and the Stop pill are the indicator people
        // actually see; the tray icon only changes appearance.
        RecordingIndicator::Shared().Show(recordingRegion_, [] {
            ScreenRecorder::Shared().Stop();
        });
    } else {
        toast::SetSuppressed(false);
        RecordingIndicator::Shared().Hide();
    }
    UpdateRecordingIndicator();
}

// The tray icon is permanent now, not just something that appears while
// recording.
//
// The original design had nothing in the tray while idle, on the grounds that
// an idle utility should be invisible. In practice that meant the only way to
// tell the program was running at all was Task Manager — and the only way to
// quit it was to find its menu first. An icon that says "this is running,
// here is its menu, here is how to quit" is worth the one shell call it
// costs. There is still no timer and no thread while idle.
void App::ShowTrayIcon() {
    if (trayIconVisible_) return;

    NOTIFYICONDATAW data{};
    data.cbSize           = sizeof(data);
    data.hWnd             = hwnd_;
    data.uID              = kTrayIconId;
    data.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = WM_TRAY_ICON;
    data.hIcon            = IdleTrayIcon();
    ::wcscpy_s(data.szTip, L"SnipTextProUltra");

    trayIconVisible_ = ::Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (!trayIconVisible_) logging::Write(L"tray: couldn't add the icon");
}

void App::HideTrayIcon() {
    if (!trayIconVisible_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd   = hwnd_;
    data.uID    = kTrayIconId;
    ::Shell_NotifyIconW(NIM_DELETE, &data);
    trayIconVisible_ = false;
}

void App::UpdateRecordingIndicator() {
    // The pill first: it is the one people are looking at.
    RecordingIndicator::Shared().Update(ScreenRecorder::Shared().ElapsedText(),
                                        recordingBlinkOn_);

    if (!trayIconVisible_) return;

    const bool recording = ScreenRecorder::Shared().IsRecording();

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd   = hwnd_;
    data.uID    = kTrayIconId;
    data.uFlags = NIF_ICON | NIF_TIP;

    // Recording swaps the icon rather than adding a second one, so the
    // tray slot never moves. The dot alternates between the red square and
    // the app icon once a second, which reads as activity — hiding the icon
    // outright would read as "the program stopped".
    data.hIcon = recording ? (recordingBlinkOn_ ? RecordingTrayIcon() : IdleTrayIcon())
                           : IdleTrayIcon();

    const std::wstring tip =
        recording ? L"SnipTextProUltra — recording " + ScreenRecorder::Shared().ElapsedText()
                  : std::wstring(L"SnipTextProUltra");
    ::wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &data);
}

void App::OnRecordingFinished(const std::wstring& path, const std::wstring& failure) {
    if (!failure.empty()) {
        ReportFailure(L"The recording couldn't be saved.\r\n\r\n" + failure);
        return;
    }
    if (path.empty()) return;
    toast::Show(L"saved " + util::LastPathComponent(path));
}

// --- settings actions ------------------------------------------------------

void App::ChooseFolder(MediaFolder& folder) {
    const std::wstring current = folder.Directory();

    BROWSEINFOW browse{};
    const std::wstring title = L"Choose where SnipText saves " + folder.Label();
    browse.hwndOwner = hwnd_;
    browse.lpszTitle = title.c_str();
    browse.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;

    PIDLIST_ABSOLUTE picked = ::SHBrowseForFolderW(&browse);
    if (!picked) return;   // cancelled: nothing at all happens

    wchar_t path[MAX_PATH]{};
    const bool resolved = ::SHGetPathFromIDListW(picked, path) != FALSE;
    ::CoTaskMemFree(picked);
    if (!resolved) return;

    folder.SetDirectory(path);
    logging::Write(folder.Label() + L": folder set to " + path);
}

void App::ChooseShutterSound() {
    wchar_t buffer[MAX_PATH]{};
    const std::wstring current = settings::GetString(settings::key::kShutterSoundPath);
    if (!current.empty() && current.size() < MAX_PATH) {
        ::wcscpy_s(buffer, current.c_str());
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize  = sizeof(dialog);
    dialog.hwndOwner    = hwnd_;
    // WAV only. PlaySound plays nothing else, and offering MP3 here would
    // mean a silent shutter with no explanation.
    dialog.lpstrFilter  = L"WAV audio\0*.wav\0All files\0*.*\0";
    dialog.lpstrFile    = buffer;
    dialog.nMaxFile     = MAX_PATH;
    dialog.lpstrTitle   = L"Choose a shutter sound";
    dialog.lpstrDefExt  = L"wav";
    // NOCHANGEDIR matters for a process that runs for weeks: without it the
    // dialog leaves the working directory wherever the user browsed, which
    // pins that volume against ejection for the rest of the session.
    dialog.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER
                        | OFN_NOCHANGEDIR;

    ::SetForegroundWindow(hwnd_);
    if (!::GetOpenFileNameW(&dialog)) return;   // cancelled: nothing changes

    settings::SetString(settings::key::kShutterSoundPath, buffer);
    // Choosing a sound implies wanting to hear it.
    settings::SetBool(settings::key::kShutterSound, true);
    logging::Write(std::wstring(L"shutter: custom sound set to ") + buffer);
    capture::PreviewShutter();
}

void App::ApplyStartup(bool enabled, int delaySeconds) {
    // Written first and unconditionally, so turning the feature off also
    // clears the delay: re-enabling later starts with none.
    settings::SetInt(settings::key::kStartupDelay, delaySeconds);

    if (!settings::SetRunAtStartup(enabled)) {
        logging::Write(enabled ? L"startup: register failed" : L"startup: unregister failed");
        ::MessageBeep(MB_ICONWARNING);
        return;
    }
    logging::Write(util::Format(L"startup: enabled=%d delay=%ds", enabled ? 1 : 0, delaySeconds));
}

void App::Sanitize() {
    // Re-enumerated at click time rather than reusing the menu's counts, so
    // the dialog describes the folders as they are right now.
    std::vector<std::wstring> files;
    const std::vector<std::wstring> screenshots = MediaFolder::Screenshots().Contents();
    const std::vector<std::wstring> textImages  = MediaFolder::TextImages().Contents();
    const std::vector<std::wstring> videos      = MediaFolder::Videos().Contents();
    files.insert(files.end(), screenshots.begin(), screenshots.end());
    files.insert(files.end(), textImages.begin(), textImages.end());
    files.insert(files.end(), videos.begin(), videos.end());

    // Itemised with live counts, so the dialog says exactly what will happen
    // rather than asking the user to trust a number they cannot check.
    std::wstring message;
    if (!files.empty()) {
        message = L"These go to the Recycle Bin, so you can put them back:\r\n";
        message += util::Format(L"    • %zu screenshot image(s)\r\n", screenshots.size());
        message += util::Format(L"    • %zu screenshot-to-text image(s)\r\n", textImages.size());
        message += util::Format(L"    • %zu video(s)\r\n\r\n", videos.size());
    }
    message += L"These settings return to their defaults:\r\n"
               L"    • the three folder locations\r\n"
               L"    • all six keyboard shortcuts\r\n"
               L"    • Text Layout, Shutter Sound, After a Screenshot, Auto-Save\r\n"
               L"    • all Screen Recording Settings\r\n"
               L"    • the annotation tool, colour and stroke width\r\n"
               L"    • Run at Startup (switched off)";

    const std::wstring heading =
        files.empty() ? L"Restore all SnipText settings to their defaults?"
                      : util::Format(L"Move %zu SnipText file%s to the Recycle Bin?",
                                     files.size(), files.size() == 1 ? L"" : L"s");

    // A task dialog rather than a message box, so the destructive button says
    // what it does instead of saying "OK", and Cancel can be the default.
    // Enter should never be the key that throws a folder of screenshots away.
    const TASKDIALOG_BUTTON buttons[] = {
        { IDOK,     files.empty() ? L"Reset" : L"Move to Recycle Bin and Reset" },
        { IDCANCEL, L"Cancel" },
    };

    TASKDIALOGCONFIG config{};
    config.cbSize             = sizeof(config);
    config.hwndParent         = hwnd_;
    config.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
    config.pszWindowTitle     = L"SnipText";
    config.pszMainIcon        = TD_WARNING_ICON;
    config.pszMainInstruction = heading.c_str();
    config.pszContent         = message.c_str();
    config.pButtons           = buttons;
    config.cButtons           = static_cast<UINT>(ARRAYSIZE(buttons));
    config.nDefaultButton     = IDCANCEL;

    ::SetForegroundWindow(hwnd_);
    int answer = IDCANCEL;
    if (FAILED(::TaskDialogIndirect(&config, &answer, nullptr, nullptr))) {
        // Fall back to a plain message box on the (unlikely) machine where
        // the task dialog is unavailable.
        answer = ::MessageBoxW(hwnd_, message.c_str(), L"SnipText",
                               MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    }
    if (answer != IDOK) {
        logging::Write(L"sanitize: cancelled");
        return;
    }

    if (!files.empty()) {
        if (media::RecycleFiles(files)) {
            logging::Write(util::Format(L"sanitize: moved %zu file(s) to the Recycle Bin", files.size()));
        } else {
            logging::Write(L"sanitize: some files couldn't be moved to the Recycle Bin");
        }
    }

    // Removal rather than assignment, so the defaults take over cleanly.
    // debugMode is deliberately untouched: it is a developer switch, not a
    // setting the user chose.
    settings::Remove(settings::key::kJoinWrappedLines);
    settings::Remove(settings::key::kShutterSound);
    settings::Remove(settings::key::kShutterSoundPath);
    settings::Remove(settings::key::kShutterTone);
    settings::Remove(settings::key::kOcrEngine);
    settings::Remove(settings::key::kSaveCaptures);
    settings::Remove(settings::key::kSkipEditor);
    settings::Remove(settings::key::kStartupDelay);
    video::RestoreDefaults();
    editor_settings::RestoreDefaults();
    hotkeys::ResetAll();
    // Re-registered immediately: the old bindings are still held by the
    // window and would otherwise keep firing until the next launch.
    hotkeys::Unregister(hwnd_);
    hotkeys::Register(hwnd_);

    // Removes the override and recreates the default directory, so the three
    // folders exist afterwards even if they didn't before.
    MediaFolder::Screenshots().SetDirectory(L"");
    MediaFolder::TextImages().SetDirectory(L"");
    MediaFolder::Videos().SetDirectory(L"");

    settings::SetRunAtStartup(false);
    lastText_.clear();   // the last OCR result is captured content too

    logging::Write(L"sanitize: settings restored to defaults");
    toast::Show(L"reset");
}

void App::ReportFailure(const std::wstring& message) {
    toast::Show(L"⚠ capture failed");
    ::SetForegroundWindow(hwnd_);
    ::MessageBoxW(hwnd_, message.c_str(), L"SnipText couldn't capture",
                  MB_OK | MB_ICONWARNING);
}

// --- editors ---------------------------------------------------------------

void App::OpenEditor(std::unique_ptr<Bitmap> image) {
    EditorWindow* editor = EditorWindow::Open(std::move(image), [this](EditorWindow* closing) {
        // Deferred: this fires from inside the window's own destruction, and
        // dropping the last reference here would free the object whose
        // window procedure is still on the stack.
        closingEditors_.push_back(closing);
        ::PostMessageW(hwnd_, WM_REAP_EDITORS, 0, 0);
    });
    if (!editor) {
        ReportFailure(L"Couldn't open the annotation editor.");
        return;
    }
    editors_.emplace_back(editor);
}

void App::ReapClosedEditors() {
    for (EditorWindow* closing : closingEditors_) {
        editors_.erase(std::remove_if(editors_.begin(), editors_.end(),
                                      [closing](const std::unique_ptr<EditorWindow>& held) {
                                          return held.get() == closing;
                                      }),
                       editors_.end());
    }
    closingEditors_.clear();
}
