// RegionOverlay.h — the full-desktop selection overlay.
//
// One window covering the whole virtual desktop, painted with a frozen
// screenshot of it. Freezing first is what lets the overlay draw a dimmed
// backdrop with a bright hole in it without appearing in its own capture, and
// it means the screen underneath cannot change halfway through a selection.
//
// Two styles share the machinery:
//
//   Instant     — drag to select, release to confirm. Space switches to
//                 click-a-whole-window. Used by both Screenshot commands.
//   Adjustable  — a resizable rectangle with eight handles and a Record
//                 button; Return confirms. Used by Record Region.
//
// The overlay runs its own modal message loop, so the caller reads as
// straight-line code and there is no half-finished selection to keep track of
// in the app's state.

#pragma once

#include "Bitmap.h"
#include "framework.h"

class RegionOverlay {
public:
    enum class Style { Instant, Adjustable };

    struct Selection {
        bool confirmed = false;
        // Pixels within the frozen desktop image, so cropping needs no
        // conversion. Add DesktopBounds().left / .top for virtual-desktop
        // coordinates, which is what the recorder wants.
        RECT bounds{};
    };

    RegionOverlay();
    RegionOverlay(const RegionOverlay&) = delete;
    RegionOverlay& operator=(const RegionOverlay&) = delete;
    ~RegionOverlay();

    // Blocks until the user confirms or cancels. Returns an unconfirmed
    // selection on Esc, on a click that never became a drag, or if the
    // desktop couldn't be captured.
    Selection Run(Style style);

    // True from the moment the overlay is put up until the caller has
    // finished acting on its result. The gap matters: the window is hidden
    // before a recording starts, and a second overlay raised inside that gap
    // would end up in the recording's first frames.
    static bool IsShowing();

    // The frozen desktop the selection was made against, and where it sits in
    // virtual-desktop coordinates. Callers crop from this rather than
    // re-grabbing the screen, so what they get is exactly what was on screen
    // when the selection was made.
    const Bitmap* FrozenDesktop() const { return frozen_.get(); }

    // Drops the frozen desktop image early. Returns null from FrozenDesktop()
    // afterwards, so call it only once everything needed has been read out.
    //
    // It exists because the recording path keeps the overlay object alive
    // after confirming — deliberately, so IsShowing() stays true while the
    // recorder starts — and the snapshot is the largest allocation in the
    // program: the whole virtual desktop at 32bpp, 33 MB per 4K monitor.
    // Holding it while the encoder spins up doubled the peak for no reason.
    void ReleaseFrozenDesktop() { frozen_.reset(); }
    RECT          DesktopBounds() const { return desktopBounds_; }

private:
    // Declaration order is hit-test priority. With a 16-pixel minimum
    // selection and hit areas grown well past the drawn handles, the boxes
    // genuinely overlap on a small selection, so the order is observable.
    enum class Grip {
        None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left
    };
    enum class DragMode { None, Drawing, Moving, Resizing };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void OnPaint(HWND hwnd);
    void OnMouseDown(HWND hwnd, POINT point);
    void OnMouseMove(HWND hwnd, POINT point);
    void OnMouseUp(HWND hwnd, POINT point);
    void OnKeyDown(HWND hwnd, WPARAM key);

    void SetSelection(HWND hwnd, const RECT& selection);
    void Confirm(HWND hwnd);
    void Cancel(HWND hwnd);

    RECT      GripRect(Grip grip) const;
    Grip      GripAt(POINT point) const;
    RECT      RecordButtonRect() const;
    RECT      HintRect() const;
    void      PickWindowUnderCursor(HWND hwnd);
    RECT      ClampToDesktop(RECT rect) const;

    void DrawChrome(HDC dc) const;

    Style                   style_ = Style::Instant;
    HWND                    hwnd_  = nullptr;
    std::unique_ptr<Bitmap> frozen_;

    // The off-screen buffer OnPaint composites into, kept between paints
    // rather than allocated per WM_MOUSEMOVE.
    //
    // A drag repaints the union of the old and new selection plus the chrome,
    // which for a large selection on a 4K screen approaches the whole desktop.
    // Allocating and freeing that DIB section on every mouse-move costs the
    // allocation, the first-touch page faults across the entire area, and the
    // free — measurably the largest slice of the per-move cost on a low-spec
    // machine.
    //
    // Grown to a high-water mark and never shrunk. Because width and height
    // grow independently, a wide drag followed by a tall one can leave a
    // buffer larger in area than either paint needed — bounded by the desktop,
    // and the overlay is stack-scoped for one capture, so nothing survives it.
    // The old code allocated a comparable buffer on every move anyway; what is
    // new is holding one for the length of a selection rather than a paint.
    std::unique_ptr<Bitmap> paintBuffer_;
    RECT                    desktopBounds_{};

    RECT     selection_{};
    bool     hasSelection_ = false;
    DragMode dragMode_     = DragMode::None;
    Grip     activeGrip_   = Grip::None;
    POINT    dragOrigin_{};
    RECT     dragStartSelection_{};
    bool     didStartNewRect_ = false;
    bool     windowPickMode_  = false;
    bool     pressedRecord_   = false;
    // Set while the overlay hides itself on purpose, so the deactivation that
    // causes is not mistaken for the user clicking away.
    bool     suppressDeactivate_ = false;
    // Set while we release capture ourselves. ReleaseCapture synchronously
    // sends WM_CAPTURECHANGED back to us, and the handler for it resets the
    // very drag state that OnMouseUp is about to read.
    bool     releasingCapture_   = false;

    Selection result_{};
    bool      finished_ = false;
};
