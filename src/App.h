// App.h — the whole application, minus the windows it opens.
//
// How this behaves as a Windows app, and why:
//
// SnipText is a pinnable taskbar program, like Snipping Tool. Pinning on
// Windows pins a shortcut to the executable, so "clicking the icon" means
// launching it. The first launch stays resident to hold the global hotkeys;
// every later launch finds the running instance, tells it to show its menu,
// and exits immediately. From the user's side there is one icon that opens
// one menu, which is the macOS menubar behaviour reproduced with the pieces
// Windows actually provides.
//
// There is no permanent tray icon and no visible window while idle. A tray
// icon appears only while recording, so the red square is a one-click Stop
// with no menu to open first — the exact role the second status item plays
// on macOS.

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

    // --- recording indicator ---
    void ShowStopIcon();
    void HideStopIcon();
    void UpdateRecordingIndicator();
    void OnRecordingStateChanged();
    void OnRecordingFinished(const std::wstring& path, const std::wstring& failure);

    // --- settings actions ---
    void ChooseFolder(MediaFolder& folder);
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

    bool         stopIconVisible_ = false;
    bool         recordingBlinkOn_ = true;
    std::wstring lastText_;   // in memory only; empty at every launch

    std::vector<std::unique_ptr<EditorWindow>> editors_;
    std::vector<EditorWindow*>                 closingEditors_;
};
