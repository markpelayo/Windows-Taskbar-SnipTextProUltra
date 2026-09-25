// RecordingIndicator.h — what tells you a recording is running.
//
// Two windows, and the split between them is deliberate:
//
//   The frame   A green dashed border drawn strictly OUTSIDE the captured
//               rectangle. It says *what* is being recorded, it is painted
//               once and then costs nothing, and because it sits outside the
//               capture it never appears in the resulting video.
//
//   The pill    A small "● 00:24  Stop" button. It says *that* a recording is
//               running, carries the elapsed time, and stops it in one click.
//               Repainted once a second — about 150x34 pixels, which is
//               nothing.
//
// Why not just the tray icon: on Windows 11 the notification area is
// collapsed behind a chevron by default, so an indicator that lives only
// there is invisible to most people — which is exactly the problem this
// solves. The tray icon stays as a second way to stop, but it is not the
// indicator.

#pragma once

#include "framework.h"

class RecordingIndicator {
public:
    static RecordingIndicator& Shared();

    // `region` is in virtual-desktop coordinates, physical pixels: the exact
    // rectangle being recorded. `onStop` fires on the UI thread when the pill
    // is clicked.
    void Show(const RECT& region, std::function<void()> onStop);
    void Hide();

    // Called once a second from the app's existing recording timer. `blinkOn`
    // alternates the dot between bright and dim — never between present and
    // absent, because a dot that vanishes reads as "stopped" and changes the
    // pill's width.
    void Update(const std::wstring& elapsed, bool blinkOn);

    bool IsShowing() const { return pill_ != nullptr; }

private:
    RecordingIndicator() = default;
    ~RecordingIndicator();
    RecordingIndicator(const RecordingIndicator&) = delete;
    RecordingIndicator& operator=(const RecordingIndicator&) = delete;

    static LRESULT CALLBACK FrameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PillProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void PaintFrame(HWND hwnd);
    void PaintPill(HWND hwnd);
    RECT ChoosePillPosition(const RECT& region);

    HWND frame_ = nullptr;
    HWND pill_  = nullptr;

    RECT         region_{};
    std::wstring elapsed_ = L"00:00";
    bool         blinkOn_ = true;
    bool         pillHot_ = false;
    bool         pillInsideCapture_ = false;

    std::function<void()> onStop_;
};
