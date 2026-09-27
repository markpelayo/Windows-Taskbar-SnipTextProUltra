// App.h — the whole application, minus the windows it opens.
//
// How this behaves as a Windows app, and why:
//
// SnipTextProUltra is a pinnable taskbar program, like Snipping Tool. Pinning on
// Windows pins a shortcut to the executable, so "clicking the icon" means
// launching it. The first launch stays resident to hold the global hotkeys;
// every later launch finds the running instance, tells it to show its menu,
// and exits immediately. From the user's side there is one icon that opens
// one menu, which is the macOS menubar behaviour reproduced with the pieces
// Windows actually provides.
//
// There is a permanent tray icon and no visible window at all. The icon
// exists so the program is visibly running and reachable: without it the only
// way to confirm it was alive was Task Manager, and the only way to quit was
// to find its menu first. Either mouse button on it opens the same menu.
//
// While recording, that one icon alternates with a red square once a second
// rather than a second icon appearing — so the tray slot never moves.

#pragma once

#include "Capture.h"
#include "EditorWindow.h"
#include "framework.h"

class MediaFolder;

class App {
public:
    static App& Shared();

    bool Run();

    // Raises the flyout menu. Called on a hotkey, and on a relaunch of the
    // pinned icon.
    void ShowMenu();

private:
    App() = default;
    ~App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    struct OcrOutcome;

    bool CreateHiddenWindow();
    void RegisterHotkeys();
    void SetUpAfterStartupDelay();
    static bool LaunchedAtLogin();

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    // --- menu ---
    HMENU BuildMenu();
    void  OnCommand(int command);

    // --- pipelines ---
    void Screenshot(capture::Mode mode);
    void ScreenshotToText(capture::Mode mode);
    void BeginRecording(bool region);
    void ToggleRecording(bool region);

    // Captures according to `mode` and returns the pixels, or nullptr when
    // the user cancelled. Both pipelines share this, so a cancel behaves
    // identically whichever command started it.
    std::unique_ptr<Bitmap> AcquireImage(capture::Mode mode);

    static DWORD WINAPI OcrThread(void* parameter);
    void OnOcrFinished(OcrOutcome* outcome);

    // --- tray icon and recording indicator ---
    void ShowTrayIcon();
    void HideTrayIcon();
    void UpdateRecordingIndicator();
    void OnRecordingStateChanged();
    void OnRecordingFinished(const std::wstring& path, const std::wstring& failure);

    // --- settings actions ---
    void ChooseFolder(MediaFolder& folder);
    void ChooseShutterSound();
    void ApplyStartup(bool enabled, int delaySeconds);
    void Sanitize();
    void ReportFailure(const std::wstring& message);

    void OpenEditor(std::unique_ptr<Bitmap> image);
    void ReapClosedEditors();

    HWND hwnd_ = nullptr;

    // Guards the whole capture path. Without it a hotkey pressed during a
    // crosshair stacks a second overlay, and two captures race to the
    // clipboard.
    bool isCapturing_ = false;

    bool         trayIconVisible_  = false;
    bool         recordingBlinkOn_ = true;
    // Virtual-desktop coordinates of whatever is being recorded, so the
    // green frame can be drawn around exactly that rectangle.
    RECT         recordingRegion_{};
    std::wstring lastText_;   // in memory only; empty at every launch

    // The endpoint IDs behind the Audio submenu's rows, captured when the
    // menu is built.
    //
    // The menu item ID encodes a POSITION in video::AvailableMicrophones(),
    // and TrackPopupMenuEx pumps the thread queue — so a WM_DEVICECHANGE
    // arriving while the submenu is open invalidates that cache, and the
    // position then refers to a different device. Plugging in a headset with
    // the menu open would select the wrong microphone, or silently nothing.
    // Resolving against this snapshot instead makes the click mean what the
    // row said when it was drawn.
    std::vector<std::wstring> menuMicrophoneIds_;

    // True while ReportFailure's message box is up.
    //
    // A modal MessageBoxW runs its own message loop, so the thread queue
    // keeps being dispatched while it is on screen. The recorder's failure
    // path has already cleared isRecording_ by then, so a record hotkey
    // pressed while the box is up used to pass every guard and stack a
    // full-desktop overlay — and a blocking 5-second JoinWorker — on top of
    // the dialog that was explaining why the LAST recording failed.
    bool reportingFailure_ = false;

    std::vector<std::unique_ptr<EditorWindow>> editors_;
    std::vector<EditorWindow*>                 closingEditors_;
};
