#include "App.h"

#include "Bitmap.h"
#include "Clipboard.h"
#include "EditorSettings.h"
#include "Log.h"
#include "MediaFolder.h"
#include "Ocr.h"
#include "RegionOverlay.h"
#include "ScreenRecorder.h"
#include "Settings.h"
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
    ID_SET_KEEPLINEBREAKS, ID_SET_SHUTTER, ID_SET_AUTOSAVE,
    ID_FOLDER_SHOT_CHOOSE = 1020, ID_FOLDER_SHOT_RESET,
    ID_FOLDER_TEXT_CHOOSE, ID_FOLDER_TEXT_RESET,
    ID_FOLDER_VIDEO_CHOOSE, ID_FOLDER_VIDEO_RESET,
    ID_VID_CURSOR = 1040, ID_VID_CLICKS,
    ID_SANITIZE = 1050, ID_QUIT,
    ID_STARTUP_OFF = 1060, ID_STARTUP_ON,
    ID_FPS_BASE      = 1100,   // + index into kFrameRateChoices
    ID_QUALITY_BASE  = 1110,   // + index
    ID_AUDIO_NONE    = 1120,
    ID_AUDIO_BASE    = 1121,   // + index into AvailableMicrophones()
    ID_DELAY_BASE    = 1200,   // + index into kStartupDelayChoices
};

const int kStartupDelayChoices[6] = { 5, 10, 15, 20, 30, 60 };

// Hotkey identifiers, numbered in menu order, top to bottom, so the menu
// itself is the reminder of what each one does.
enum : int { HK_SHOT_REGION = 1, HK_SHOT_FULL, HK_TEXT_REGION, HK_TEXT_FULL,
             HK_RECORD_REGION, HK_RECORD_FULL };

UINT RelaunchMessage() {
    static const UINT message = ::RegisterWindowMessageW(L"SnipTextProUltra.ShowMenu");
    return message;
}

// Explorer broadcasts this after it restarts. Without handling it, an
// Explorer crash mid-recording would take the Stop icon with it permanently
// while the app went on believing the icon was there.
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

void AppendSubmenu(HMENU parent, HMENU child, const std::wstring& title) {
    // A null child would produce an MF_POPUP item that cannot be opened —
    // worse than the row simply not being there.
    if (!parent || !child) return;
    ::AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(child), title.c_str());
}

// "Show Saved Images (12)" or "Show Saved Images — none yet". The count is
// part of the label because the alternative is opening a folder to find out
// it is empty.
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

} // namespace

// ---------------------------------------------------------------------------

struct App::OcrOutcome {
    std::unique_ptr<Bitmap> image;
    capture::Mode           mode = capture::Mode::Region;
    bool                    keepLineBreaks = false;
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

    log::SetVerbose(settings::GetBool(settings::key::kDebugMode, false));
    log::StartSession(L"SnipText launched, log at " + log::FilePath());

    if (!gdip::Startup()) {
        log::Write(L"launch: GDI+ failed to start");
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
        log::Write(util::Format(L"startup: login launch, holding setup for %ds", delay));
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
    toast::Destroy();
    gdip::Shutdown();
    log::Shutdown();
    return true;
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
    hwnd_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"SnipText", WS_POPUP,
                              0, 0, 0, 0, nullptr, nullptr,
                              ::GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        log::Write(L"launch: couldn't create the main window");
        return false;
    }
    return true;
}

void App::SetUpAfterStartupDelay() {
    RegisterHotkeys();
}

void App::RegisterHotkeys() {
    struct Binding { int id; UINT key; };
    // Win+Alt+1 through 6, numbered in menu order.
    //
    // Worth knowing: the Windows shell already uses Win+Alt+<digit> to open
    // the Jump List of the pinned taskbar app in that position. Whichever
    // process registers first wins, and the shell is always first, so these
    // registrations can simply fail. When one does, that shortcut does
    // nothing for the session and the log names it — the menu item still
    // works. See ARCHITECTURE.md for the alternatives.
    const Binding bindings[] = {
        { HK_SHOT_REGION,   '1' }, { HK_SHOT_FULL,     '2' },
        { HK_TEXT_REGION,   '3' }, { HK_TEXT_FULL,     '4' },
        { HK_RECORD_REGION, '5' }, { HK_RECORD_FULL,   '6' },
    };

    for (const Binding& binding : bindings) {
        // MOD_NOREPEAT: a held key should fire once, not open a crosshair per
        // repeat tick.
        if (!::RegisterHotKey(hwnd_, binding.id, MOD_WIN | MOD_ALT | MOD_NOREPEAT,
                              binding.key)) {
            // A taken shortcut fails silently and permanently for this
            // launch; the menu item still works. Surfacing a dialog at
            // startup for something the user can neither see nor fix would be
            // worse than a log line.
            log::Write(util::Format(L"hotkey: Win+Alt+%c is already taken — most likely by "
                                    L"the shell's Jump List shortcut. Use the menu item instead.",
                                    static_cast<wchar_t>(binding.key)));
        }
    }
}

bool App::LaunchedAtLogin() {
    if (!settings::IsRunAtStartupEnabled()) {
        log::Write(L"startup: not registered to run at startup, launching now");
        return false;
    }
    // Startup entries fire moments after the desktop appears, so a launch
    // inside the first two minutes of uptime is a startup launch and anything
    // later is the user. The bias is deliberate: a missing delay is
    // invisible, while a wrong one looks like a broken app.
    const ULONGLONG uptimeMs = ::GetTickCount64();
    const bool isLogin = uptimeMs < 120000;
    log::Write(util::Format(L"startup: uptime %llus — treating as a %s launch",
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
        if (ScreenRecorder::Shared().IsRecording()) {
            stopIconVisible_ = false;   // the old icon went with Explorer
            ShowStopIcon();
        }
        return 0;
    }

    switch (message) {
    case WM_HOTKEY:
        switch (static_cast<int>(wParam)) {
        case HK_SHOT_REGION:   Screenshot(capture::Mode::Region); return 0;
        case HK_SHOT_FULL:     Screenshot(capture::Mode::FullScreen); return 0;
        case HK_TEXT_REGION:   ScreenshotToText(capture::Mode::Region); return 0;
        case HK_TEXT_FULL:     ScreenshotToText(capture::Mode::FullScreen); return 0;
        // Both recording hotkeys are toggles. Once the overlay is gone the
        // only feedback is the tray indicator, and a toggle is what you reach
        // for then — so either one stops a recording, whichever started it.
        case HK_RECORD_REGION: ToggleRecording(true); return 0;
        case HK_RECORD_FULL:   ToggleRecording(false); return 0;
        }
        return 0;

    case WM_TRAY_ICON:
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            // A single click stops immediately; there is no menu to open
            // first, which is the whole point of the icon existing.
            ScreenRecorder::Shared().Stop();
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
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
        for (int id = HK_SHOT_REGION; id <= HK_RECORD_FULL; ++id) ::UnregisterHotKey(hwnd_, id);
        HideStopIcon();
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

    AppendHeader(menu, L"Screenshot");
    AppendCommand(menu, ID_SHOT_REGION, L"Capture Region…\tWin+Alt+1");
    AppendCommand(menu, ID_SHOT_FULL,   L"Capture Full Screen\tWin+Alt+2");
    AppendCommand(menu, ID_SHOT_SHOW, SavedItemTitle(L"Show Saved Images", screenshotCount),
                  screenshotCount > 0);
    AppendSeparator(menu);

    AppendHeader(menu, L"Screenshot to Text");
    AppendCommand(menu, ID_TEXT_REGION, L"Capture Region…\tWin+Alt+3");
    AppendCommand(menu, ID_TEXT_FULL,   L"Capture Full Screen\tWin+Alt+4");
    if (lastText_.empty()) {
        AppendCommand(menu, ID_TEXT_COPYLAST, L"No text captured yet", false);
    } else {
        // Capped at 14 characters because this is the one label whose width
        // varies with the user's data; a generous cap would make the menu
        // change width every time it was used.
        AppendCommand(menu, ID_TEXT_COPYLAST,
                      L"Copy: “" + Preview(lastText_, 14) + L"”");
    }
    AppendCommand(menu, ID_TEXT_SHOW, SavedItemTitle(L"Show Saved Images", textImageCount),
                  textImageCount > 0);
    AppendSeparator(menu);

    AppendHeader(menu, L"Record Video");
    if (recording) {
        AppendCommand(menu, ID_REC_STOP,
                      L"Stop Recording (" + ScreenRecorder::Shared().ElapsedText() + L")");
    } else {
        AppendCommand(menu, ID_REC_REGION, L"Record Region…\tWin+Alt+5");
        AppendCommand(menu, ID_REC_FULL,   L"Record Full Screen\tWin+Alt+6");
    }
    AppendCommand(menu, ID_REC_SHOW, SavedItemTitle(L"Show Saved Videos", videoCount),
                  videoCount > 0);
    AppendSeparator(menu);

    AppendHeader(menu, L"Settings");
    AppendCommand(menu, ID_SET_KEEPLINEBREAKS, L"Keep Line Breaks", true,
                  settings::GetBool(settings::key::kKeepLineBreaks, false));
    AppendCommand(menu, ID_SET_SHUTTER, L"Shutter Sound", true,
                  settings::GetBool(settings::key::kShutterSound, true));
    AppendCommand(menu, ID_SET_AUTOSAVE, L"Auto-Save Images", true,
                  settings::GetBool(settings::key::kSaveCaptures, false));

    // --- the three folder submenus ---
    struct FolderEntry { MediaFolder& folder; const wchar_t* title; int chooseId; int resetId; };
    const FolderEntry folderEntries[] = {
        { MediaFolder::Screenshots(), L"Screenshot Folder",
          ID_FOLDER_SHOT_CHOOSE, ID_FOLDER_SHOT_RESET },
        // Deliberately not "Screenshot to Text Folder": it reads as a set
        // with the other two, and the menu is only ever as narrow as its
        // longest label.
        { MediaFolder::TextImages(), L"Text Folder",
          ID_FOLDER_TEXT_CHOOSE, ID_FOLDER_TEXT_RESET },
    };

    auto appendFolderSubmenu = [&](const FolderEntry& entry) {
        HMENU submenu = ::CreatePopupMenu();
        if (!submenu) return;
        ::AppendMenuW(submenu, MF_STRING | MF_GRAYED, 0, entry.folder.DisplayPath().c_str());
        AppendSeparator(submenu);
        AppendCommand(submenu, entry.chooseId, L"Choose Folder…");
        AppendCommand(submenu, entry.resetId, L"Reset to Default",
                      !entry.folder.IsUsingDefaultDirectory());

        // No "Default" suffix while at the default location: the folder name
        // only appears once it carries information.
        std::wstring title = entry.folder.IsUsingDefaultDirectory()
            ? std::wstring(entry.title)
            : std::wstring(entry.title) + L": "
              + util::LastPathComponent(entry.folder.Directory());
        AppendSubmenu(menu, submenu, title);
    };

    for (const FolderEntry& entry : folderEntries) appendFolderSubmenu(entry);

    // --- Video Settings, which sits between Text Folder and Video Folder ---
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

            AppendSubmenu(menu, videoMenu, L"Video Settings");
        }
    }

    appendFolderSubmenu({ MediaFolder::Videos(), L"Video Folder",
                          ID_FOLDER_VIDEO_CHOOSE, ID_FOLDER_VIDEO_RESET });

    // --- Sanitize ---
    const bool atDefaults =
        MediaFolder::Screenshots().IsUsingDefaultDirectory()
        && MediaFolder::TextImages().IsUsingDefaultDirectory()
        && MediaFolder::Videos().IsUsingDefaultDirectory()
        && !settings::GetBool(settings::key::kKeepLineBreaks, false)
        &&  settings::GetBool(settings::key::kShutterSound, true)
        && !settings::GetBool(settings::key::kSaveCaptures, false)
        &&  settings::GetInt(settings::key::kStartupDelay, 0) == 0
        && !settings::IsRunAtStartupEnabled()
        &&  video::IsDefault()
        &&  editor_settings::IsDefault()
        &&  lastText_.empty();
    const int totalFiles = screenshotCount + textImageCount + videoCount;
    AppendCommand(menu, ID_SANITIZE, L"Sanitize and Restore Default…",
                  totalFiles > 0 || !atDefaults);
    AppendSeparator(menu);

    // --- Startup ---
    AppendHeader(menu, L"Startup");
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
            // Only attached if the submenu was actually created; an MF_POPUP
            // item with a null handle is a menu item that cannot be used.
            AppendSubmenu(menu, startup, title);
        }
    }
    AppendSeparator(menu);

    AppendCommand(menu, ID_QUIT, L"Quit SnipText");
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
    if (command >= ID_DELAY_BASE && command < ID_DELAY_BASE + 6) {
        ApplyStartup(true, kStartupDelayChoices[command - ID_DELAY_BASE]);
        return;
    }
    if (command >= ID_AUDIO_BASE) {
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

    case ID_SET_KEEPLINEBREAKS:
        settings::SetBool(settings::key::kKeepLineBreaks,
                          !settings::GetBool(settings::key::kKeepLineBreaks, false));
        return;
    case ID_SET_SHUTTER:
        settings::SetBool(settings::key::kShutterSound,
                          !settings::GetBool(settings::key::kShutterSound, true));
        return;
    case ID_SET_AUTOSAVE: {
        const bool on = !settings::GetBool(settings::key::kSaveCaptures, false);
        settings::SetBool(settings::key::kSaveCaptures, on);
        log::Write(on ? L"save images on" : L"save images off");
        return;
    }

    case ID_FOLDER_SHOT_CHOOSE:  ChooseFolder(MediaFolder::Screenshots()); return;
    case ID_FOLDER_TEXT_CHOOSE:  ChooseFolder(MediaFolder::TextImages()); return;
    case ID_FOLDER_VIDEO_CHOOSE: ChooseFolder(MediaFolder::Videos()); return;
    case ID_FOLDER_SHOT_RESET:
        MediaFolder::Screenshots().SetDirectory(L"");
        log::Write(L"screenshots: folder reset to " + MediaFolder::Screenshots().Directory());
        return;
    case ID_FOLDER_TEXT_RESET:
        MediaFolder::TextImages().SetDirectory(L"");
        log::Write(L"text images: folder reset to " + MediaFolder::TextImages().Directory());
        return;
    case ID_FOLDER_VIDEO_RESET:
        MediaFolder::Videos().SetDirectory(L"");
        log::Write(L"videos: folder reset to " + MediaFolder::Videos().Directory());
        return;

    case ID_VID_CURSOR: video::SetCapturesCursor(!video::CapturesCursor()); return;
    case ID_VID_CLICKS: video::SetCapturesClicks(!video::CapturesClicks()); return;
    case ID_AUDIO_NONE: video::SetAudioDeviceId(L""); return;

    case ID_STARTUP_OFF: ApplyStartup(false, 0); return;
    case ID_STARTUP_ON:  ApplyStartup(true, 0); return;

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
    if (settings::GetBool(settings::key::kSaveCaptures, false)) {
        std::vector<BYTE> png = image->EncodePng();
        if (!png.empty()) MediaFolder::Screenshots().SaveBytes(png.data(), png.size());
    }

    log::Write(util::Format(L"screenshot: editor opened %dx%d (%s)",
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

    const bool keepLineBreaks = settings::GetBool(settings::key::kKeepLineBreaks, false);
    LOG_DEBUG(util::Format(L"pipeline: start mode=%s keepLineBreaks=%d",
                           capture::ModeLabel(mode), keepLineBreaks ? 1 : 0));

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

    ScopedHandle thread(::CreateThread(nullptr, 0, &App::OcrThread, outcome, 0, nullptr));
    if (!thread) {
        delete outcome;
        isCapturing_ = false;
        ReportFailure(L"Couldn't start text recognition.");
    }
    // `isCapturing_` stays true until the result lands, so a second Win+Alt+3
    // during recognition is ignored rather than racing to the clipboard.
}

DWORD WINAPI App::OcrThread(void* parameter) {
    auto* outcome = static_cast<OcrOutcome*>(parameter);

    ocr::Result recognised = ocr::Recognize(*outcome->image);
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
        log::Write(L"pipeline: failure — " + outcome->failure);
        ReportFailure(outcome->failure);
        return;
    }

    if (outcome->text.empty()) {
        log::Write(L"capture produced no readable text");
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
    log::Write(util::Format(L"copied %zu chars from %zu lines (%s)",
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

        // The overlay's window is already hidden at this point, and the
        // overlay object is still alive, so IsShowing() still reports true —
        // which is what stops a second overlay appearing in the first frames
        // of the recording about to start.
        const std::wstring failure = ScreenRecorder::Shared().Start(target);
        if (!failure.empty()) ReportFailure(failure);
        return;
    }

    target = util::MonitorBounds(util::MonitorUnderCursor());
    const std::wstring failure = ScreenRecorder::Shared().Start(target);
    if (!failure.empty()) ReportFailure(failure);
}

void App::OnRecordingStateChanged() {
    ::KillTimer(hwnd_, kRecordingTimer);

    if (ScreenRecorder::Shared().IsRecording()) {
        recordingBlinkOn_ = true;
        // One timer drives both the elapsed text and the blink, so the two
        // can never drift out of step.
        ::SetTimer(hwnd_, kRecordingTimer, 1000, nullptr);
        toast::SetSuppressed(true);
        ShowStopIcon();
    } else {
        toast::SetSuppressed(false);
        HideStopIcon();
    }
    UpdateRecordingIndicator();
}

void App::ShowStopIcon() {
    if (stopIconVisible_) return;

    // Created once and intentionally never destroyed. A function-local static
    // holding an HICON would run its destructor at CRT exit, after GDI
    // teardown; one icon handle for the process is the cheaper trade.
    static HICON icon = MakeStopIcon().release();
    if (!icon) return;

    NOTIFYICONDATAW data{};
    data.cbSize           = sizeof(data);
    data.hWnd             = hwnd_;
    data.uID              = kTrayIconId;
    data.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = WM_TRAY_ICON;
    data.hIcon            = icon;
    ::wcscpy_s(data.szTip, L"SnipText — recording. Click to stop.");

    stopIconVisible_ = ::Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void App::HideStopIcon() {
    if (!stopIconVisible_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd   = hwnd_;
    data.uID    = kTrayIconId;
    ::Shell_NotifyIconW(NIM_DELETE, &data);
    stopIconVisible_ = false;
}

void App::UpdateRecordingIndicator() {
    if (!stopIconVisible_) return;

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd   = hwnd_;
    data.uID    = kTrayIconId;
    data.uFlags = NIF_TIP | NIF_STATE;
    // Blink by hiding and showing the icon rather than by swapping icons: it
    // costs no second icon handle, and the tray reserves the slot either way
    // so nothing shuffles along the taskbar once a second.
    data.dwState     = recordingBlinkOn_ ? 0 : NIS_HIDDEN;
    data.dwStateMask = NIS_HIDDEN;
    const std::wstring tip = L"SnipText — recording "
                           + ScreenRecorder::Shared().ElapsedText()
                           + L". Click to stop.";
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
    log::Write(folder.Label() + L": folder set to " + path);
}

void App::ApplyStartup(bool enabled, int delaySeconds) {
    // Written first and unconditionally, so turning the feature off also
    // clears the delay: re-enabling later starts with none.
    settings::SetInt(settings::key::kStartupDelay, delaySeconds);

    if (!settings::SetRunAtStartup(enabled)) {
        log::Write(enabled ? L"startup: register failed" : L"startup: unregister failed");
        ::MessageBeep(MB_ICONWARNING);
        return;
    }
    log::Write(util::Format(L"startup: enabled=%d delay=%ds", enabled ? 1 : 0, delaySeconds));
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
               L"    • Keep Line Breaks, Shutter Sound, Auto-Save Images\r\n"
               L"    • all Video Settings\r\n"
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
        log::Write(L"sanitize: cancelled");
        return;
    }

    if (!files.empty()) {
        if (media::RecycleFiles(files)) {
            log::Write(util::Format(L"sanitize: moved %zu file(s) to the Recycle Bin", files.size()));
        } else {
            log::Write(L"sanitize: some files couldn't be moved to the Recycle Bin");
        }
    }

    // Removal rather than assignment, so the defaults take over cleanly.
    // debugMode is deliberately untouched: it is a developer switch, not a
    // setting the user chose.
    settings::Remove(settings::key::kKeepLineBreaks);
    settings::Remove(settings::key::kShutterSound);
    settings::Remove(settings::key::kSaveCaptures);
    settings::Remove(settings::key::kStartupDelay);
    video::RestoreDefaults();
    editor_settings::RestoreDefaults();

    // Removes the override and recreates the default directory, so the three
    // folders exist afterwards even if they didn't before.
    MediaFolder::Screenshots().SetDirectory(L"");
    MediaFolder::TextImages().SetDirectory(L"");
    MediaFolder::Videos().SetDirectory(L"");

    settings::SetRunAtStartup(false);
    lastText_.clear();   // the last OCR result is captured content too

    log::Write(L"sanitize: settings restored to defaults");
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
