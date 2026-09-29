// EditorWindow.h — the annotation editor.
//
// Nothing is baked into the image until you copy or save. Every mark stays
// selectable, movable, resizable and restylable, and undo works on all of
// those on the same footing as drawing, because undo stores whole-array
// snapshots rather than a list of added shapes.

#pragma once

#include "Annotation.h"
#include "Bitmap.h"
#include "framework.h"

// Forward-declared rather than including gdiplus.h here. Gdiplus::Bitmap and
// our own ::Bitmap have the same unqualified name, and pulling the GDI+
// headers into every translation unit that touches the editor is how that
// collision spreads.
namespace Gdiplus { class Graphics; class Bitmap; }

class EditorWindow {
public:
    // Called once, on the UI thread, when the window has closed. The owner
    // must defer the actual destruction to a later message-loop turn: this
    // fires from inside the window's own teardown.
    using CloseCallback = std::function<void(EditorWindow*)>;

    static EditorWindow* Open(std::unique_ptr<Bitmap> image, CloseCallback onClose);

    // Offered every keyboard message before it is dispatched, and returns
    // true when it consumed one.
    //
    // This has to happen in the message loop rather than in a window
    // procedure, because the editor is a frame full of child controls and
    // a key only ever reaches the one with focus. Handling Esc in the
    // canvas meant Esc worked on the canvas and nowhere else — click a
    // tool button first and the key went to the button, which drops it.
    // From here it does not matter what has focus, only that the message
    // is bound for this editor's window tree, which for keyboard input is
    // the same statement as "this editor is the active window".
    static bool PreTranslateMessage(const MSG& message);

    // Repaints the Pin toggle in every open editor. Called by the editor's
    // own toggle and by the tray row, because they write one setting and
    // two windows must not disagree about what it says.
    static void PinSettingChanged();

    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;
    ~EditorWindow();

private:
    enum class DragMode { None, Drawing, Moving, Resizing };

    EditorWindow(std::unique_ptr<Bitmap> image, CloseCallback onClose);

    bool Create();

    static LRESULT CALLBACK FrameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SwatchProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TextEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT OnFrameMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnSwatchMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Every editor currently open. A file-static would do, except
    // PinSettingChanged has to reach a private member of each one.
    static std::vector<EditorWindow*>& LiveEditors();

    void LayoutChildren();
    void PaintCanvas(HDC dc);
    void RefreshToolbarState();
    // The Pin button is icon-only, so its tooltip is the only place the
    // state appears in words — and therefore has to be rewritten whenever
    // the setting changes, not set once at creation.
    void UpdatePinTooltip();
    // Puts this window above or below the others, from the shared setting.
    // The entirety of what "Pin to Screen" now does.
    void ApplyAlwaysOnTop();
    void ReturnFocusToCanvas();

    // Runs the editor-wide key bindings. Returns true if the key was
    // used, so the caller knows not to dispatch it.
    bool HandleEditorKey(UINT key, UINT modifiers);

    // What the pointer should look like at a given canvas point, resolved
    // in the same order WM_LBUTTONDOWN resolves a click — so the cursor is
    // a promise about what clicking will do rather than a decoration.
    HCURSOR CursorForPoint(POINT view) const;
    static HCURSOR CursorForHandle(Handle handle);

    // The colour a new mark gets. There were briefly two behind one
    // swatch — ink, and a cover colour for the Redact tool — and folding
    // that tool into Shift-drag took the second one with it.
    COLORREF ActiveColour() const;

    // --- coordinate mapping ---
    // The canvas shows the image scaled to fit; annotations are stored in
    // image pixels, so every input point crosses this boundary exactly once.
    double ImageScale() const;
    RECT   ImageRect() const;
    PointD ToImagePoint(POINT view) const;
    POINT  ToViewPoint(PointD image) const;

    // --- editing ---
    void Snapshot();
    void SnapshotStyleChangeIfNeeded();
    void TakeDragSnapshotIfNeeded();
    void Undo();
    void Redo();
    void DeleteSelection();
    void ClearSelection();

    void SetCurrentTool(Tool tool);
    void SetCurrentColour(COLORREF colour);
    void SetCurrentLineWidth(double width);

    // --- text entry ---
    void BeginTextEntry(PointD anchor);
    // A left-pointing callout's label is positioned by its RIGHT edge, so
    // its left edge moves every time a character is typed. Without this the
    // field grows rightwards while the committed label grows leftwards, and
    // the text jumps the full width of the string on commit — the exact
    // thing laying the field out in the final position is meant to prevent.
    void RepositionCalloutField();
    void CommitTextEntry();
    bool CancelTextEntry();   // true when there was a label, and it was discarded

    // --- colour popup ---
    void ShowColourPopup();
    void HideColourPopup();
    void OpenSystemColourPicker();

    // --- export ---
    std::unique_ptr<Bitmap> Flatten();
    void CopyToClipboard();
    void SaveAsPng();
    void FlashTitle(const wchar_t* note);

    // --- state ---
    std::unique_ptr<Bitmap> image_;

    // The off-screen buffer PaintCanvas composites into, kept between paints.
    //
    // PaintCanvas runs on every WM_MOUSEMOVE while drawing, and allocating a
    // canvas-sized DIB section each time costs the allocation, the
    // first-touch page faults over the whole canvas, and the free.
    //
    // Sized to match the canvas exactly, so it is reallocated on a resize and
    // never during a drag. Not grown to a high-water mark: that would keep the
    // buffer at the largest size the window had ever been, so maximising and
    // restoring would leave the bigger allocation resident for as long as the
    // editor stayed open.
    //
    // The cost is one canvas-sized bitmap held while an editor window is open,
    // where before it existed only during a paint.
    std::unique_ptr<Bitmap> paintBuffer_;

    // The capture, scaled once to the size it is drawn at.
    //
    // Why it exists: the canvas used to re-scale the full-resolution capture
    // on EVERY WM_MOUSEMOVE, and because the good resampler is too slow to do
    // that, it switched to a crude one mid-drag. The crude one DROPS source
    // rows and columns instead of averaging them, so one-pixel glyph stems
    // disappeared and came back as the mode flipped on mouse-down and
    // mouse-up — text that appeared to vibrate while drawing.
    //
    // Scaling once removes the reason for the crude mode, so the good one is
    // used always. The wobble goes, and drawing gets faster rather than
    // slower.
    //
    // Bounded by construction, which is the point:
    //   - One bitmap, canvas-sized. ~4 MB for a typical window.
    //   - Drawing, undo, paste and save never touch it. It cannot grow with
    //     use; nothing is ever appended.
    //   - A resize REPLACES it, releasing the old one first, so shrinking the
    //     window shrinks this too rather than leaving the larger one resident.
    //   - Released entirely when the capture is shown at 1:1, because then
    //     there is nothing to scale and the original can be blitted directly.
    //     A small capture therefore costs nothing at all.
    //   - Freed with the window.
    std::unique_ptr<Bitmap> scaledImage_;

    // The colour to paint over a region that has been lifted away with Shift.
    // Sampled from the ring of pixels just outside the region, because that is
    // what the hole should look like if it is to disappear: the background the
    // region was sitting on. On a flat background it is exact. On a gradient
    // or a photograph nothing flat can be right, which is why plain drag —
    // which never leaves a hole — is the default.
    COLORREF DominantEdgeColour(const RectD& region) const;

    // A GDI+ view of image_'s pixels, shared rather than copied, for Lift to
    // read from. Null when no mark needs it. Never cached: it borrows the
    // DIB's buffer and must not outlive it.
    std::unique_ptr<Gdiplus::Bitmap> PictureForLift() const;
    CloseCallback           onClose_;

    HWND hwnd_       = nullptr;
    HWND canvas_     = nullptr;
    HWND swatch_     = nullptr;
    HWND slider_     = nullptr;
    HWND textEdit_   = nullptr;
    HWND colourPopup_ = nullptr;
    HWND toolButtons_[kToolCount]{};
    HWND undoButton_ = nullptr;
    HWND redoButton_ = nullptr;
    HWND copyButton_ = nullptr;
    HWND saveButton_ = nullptr;
    // Toggles whether the editor stays above other windows — the same
    // setting as the tray row. Thirteen icon buttons need the tooltips, or
    // the bar is a rebus.
    HWND pinButton_ = nullptr;
    HWND tooltips_  = nullptr;

    std::vector<Annotation>              annotations_;
    std::vector<std::vector<Annotation>> undoStack_;
    std::vector<std::vector<Annotation>> redoStack_;

    int      selectedIndex_ = -1;
    Tool     currentTool_   = Tool::Arrow;
    COLORREF currentColour_ = RGB(52, 199, 89);
    double   currentLineWidth_ = 4.0;

    DragMode   dragMode_ = DragMode::None;
    Handle     activeHandle_ = Handle::None;
    RectD      resizeOriginalRect_{};
    PointD     dragLastPoint_{};
    bool       needsSnapshotBeforeDrag_ = false;
    bool       hasDraft_ = false;
    Annotation draft_;

    ULONGLONG lastStyleChangeAt_ = 0;

    PointD       textAnchor_{};
    COLORREF     textEntryColour_ = RGB(52, 199, 89);
    bool         textEntryActive_ = false;
    // While a callout's label is being typed, the index of the arrow it
    // belongs to. -1 means the entry is a standalone Text mark, which is the
    // only case there used to be. The arrow is pushed BEFORE the label is
    // typed, so that cancelling leaves a plain arrow rather than nothing —
    // you drew it, so it should still be there.
    int          calloutIndex_    = -1;
    ScopedFont   textFont_;
    WNDPROC      textEditOriginalProc_ = nullptr;

    ULONGLONG    popupClosedAt_ = 0;
    std::wstring baseTitle_;
};
