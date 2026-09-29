// PinnedWindow.h — a capture stuck to the screen.
//
// A borderless topmost window holding one Bitmap. Drag it anywhere, scroll to
// resize it, double-click to hand it to the editor, right-click for the rest.
// It exists so a reference — an error message, a part number, a diagram — can
// stay visible while you work in another window, which is the one thing a
// screenshot in a clipboard cannot do.
//
// Deliberately NOT an EditorWindow with the chrome hidden. The editor owns an
// undo stack, an annotation array, a scaled cache and two toolbars; this owns
// a bitmap and a rectangle. Reusing the editor would have made every pinned
// capture cost what an open editor costs, and a pin is meant to be cheap
// enough that leaving four of them lying around is not a decision.

#pragma once

#include "Bitmap.h"
#include "framework.h"

class PinnedWindow {
public:
    // Called once, on the UI thread, when the window has closed. As with
    // EditorWindow the owner must defer the actual destruction to a later
    // message-loop turn: this fires from inside the window's own teardown.
    using CloseCallback = std::function<void(PinnedWindow*)>;
    // Raised when the user asks for the editor. The bitmap handed over is a
    // COPY, so closing the pin afterwards cannot pull the pixels out from
    // under the editor.
    using EditCallback  = std::function<void(std::unique_ptr<Bitmap>)>;

    static PinnedWindow* Open(std::unique_ptr<Bitmap> image,
                              const RECT& capturedFrom,
                              CloseCallback onClose,
                              EditCallback onEdit);

    PinnedWindow(const PinnedWindow&) = delete;
    PinnedWindow& operator=(const PinnedWindow&) = delete;
    ~PinnedWindow();

    // How many are on screen right now. The tray menu reports it, and the
    // "close them all" row is disabled when it is zero.
    static int  LiveCount();
    static void CloseAll();

private:
    PinnedWindow(std::unique_ptr<Bitmap> image, CloseCallback onClose, EditCallback onEdit);

    bool Create(const RECT& capturedFrom);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // Takes the handle from the message rather than reading hwnd_, because
    // the last few messages a window receives arrive after hwnd_ has been
    // cleared and DefWindowProc still needs somewhere to send them.
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void ShowContextMenu(POINT screen);
    void ApplyScale(double scale, POINT anchorScreen);
    void CopyToClipboard();
    void SaveAsPng();
    void HandToEditor();

    // The pixels, at full resolution. Never resampled in place — zooming
    // changes the window size and the blit, so zooming out and back in again
    // returns to the original sharpness rather than compounding losses.
    std::unique_ptr<Bitmap> image_;

    CloseCallback onClose_;
    EditCallback  onEdit_;

    HWND   hwnd_  = nullptr;
    double scale_ = 1.0;

    // Set on mouse-down, cleared on mouse-up. The offset is where in the
    // window the grab happened, so the window follows the pointer without
    // jumping its top-left corner to it.
    bool  dragging_   = false;
    POINT dragOffset_{};
};
