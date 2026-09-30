// EditorWindow.h — the annotation editor.
//
// Nothing is baked into the image until you copy or save. Every mark stays
// selectable, movable, resizable and restylable, and undo works on all of
// those on the same footing as drawing, because undo stores whole-STATE
// snapshots rather than a list of added shapes — the annotation array and
// the crop rectangle together, so cropping is an undoable edit like any
// other.

#pragma once

#include "Annotation.h"
#include "Bitmap.h"
#include "Util.h"
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
    enum class DragMode { None, Drawing, Moving, Resizing,
                          // Swinging a label around its mark. A mode of its
                          // own because the thing being moved is not the
                          // mark and not a handle.
                          MovingLabel };

    EditorWindow(std::unique_ptr<Bitmap> image, CloseCallback onClose);

    bool Create();

    static LRESULT CALLBACK FrameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SwatchProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // The width slider is ours now rather than a comctl32 trackbar. The
    // trackbar's thumb shape is the system's and cannot be restyled, and it
    // was also the reason the thumb sat above the track: without TBS_BOTH
    // Windows gives a horizontal trackbar a downward-POINTING thumb and
    // leaves room for it by pushing the channel up.
    static LRESULT CALLBACK SliderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TextEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT OnFrameMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnSwatchMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnSliderMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // Maps a pointer x within the slider to a stroke width, and back.
    double SliderValueForX(int x) const;
    int    SliderXForValue(double value) const;

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
    // Z-order. `delta` is -1 or +1 for one step; `toEnd` sends it all the
    // way instead. The annotation array is the z-order, so this is a move
    // within the array and nothing else.
    void MoveSelection(int delta, bool toEnd);
    // Arrow-key movement, in image pixels.
    void NudgeSelection(double dx, double dy);
    void DeleteSelection();
    void ClearSelection();

    void SetCurrentTool(Tool tool);
    void SetCurrentColour(COLORREF colour);
    // `persist` is false while the slider is being dragged: the registry
    // would otherwise take a write per pixel of travel, about a hundred per
    // drag, for a value only the last of which matters.
    void SetCurrentLineWidth(double width, bool persist = true);

    // --- text entry ---
    void BeginTextEntry(PointD anchor);
    // Opens the inline field on an existing mark's label — F2, or a
    // double-click. On a Tool::Text mark it edits the text itself, which is
    // the same operation: both are "the string the user typed".
    void BeginLabelEntry(int index);
    // A label is centred on its ray, so its left edge walks with every
    // character typed in every direction except due east. The field has to
    // follow, or the text jumps the width of the string on commit.
    void RepositionLabelField();
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
    // Which crop scaledImage_ was built from. Part of its cache key: two
    // different crops can produce the same destination size, and without
    // this the canvas would keep showing the region it was scaled from.
    RECT scaledFrom_{};

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

    // What the crop tool changes, and why it is a RECTANGLE rather than a
    // smaller bitmap.
    //
    // Cropping destructively — replacing image_ with a cut-down copy and
    // shifting every annotation — would mean the undo stack had to hold a
    // bitmap per step to be reversible. At 33 MB for a 4K capture and a cap
    // of fifty steps, that is a gigabyte and a half of undo history for a
    // screenshot editor.
    //
    // A rectangle costs sixteen bytes. The capture is never modified, marks
    // keep the coordinates they were drawn in, a mark that falls outside the
    // crop still exists and comes back if you undo, and re-cropping is just
    // another rectangle. Everything downstream — the canvas, the export, the
    // title — reads the crop instead of the image's own size.
    //
    // In ORIGINAL image pixels, always. Starts as the whole capture.
    RECT crop_{};

    int CropWidth()  const { return util::RectWidth(crop_); }
    int CropHeight() const { return util::RectHeight(crop_); }

    // Applies a crop and pushes the previous state for undo. `region` is in
    // original image coordinates and is clamped to the current crop: you can
    // only ever narrow the view, never widen it by dragging.
    // Returns false when the region was rejected — too small, or already
    // the crop — so the caller knows the gesture did nothing and can leave
    // the tool armed for another try.
    bool ApplyCrop(const RectD& region);

    // Is this mark inside the visible picture at all? Used to keep a mark
    // that a crop pushed out of view from being clickable in the grey
    // letterbox beside the image.
    bool IsWithinCrop(const Annotation& annotation, Gdiplus::Graphics* measureWith) const;
    // The title carries the size, and a crop changes it.
    void UpdateTitleForCrop();

    // One undo step. The crop travels with the annotations because a crop
    // IS an edit — Ctrl+Z after cropping has to put the picture back, not
    // just the marks.
    struct EditorState {
        std::vector<Annotation> annotations;
        RECT                    crop{};
    };

    std::vector<Annotation>  annotations_;
    std::vector<EditorState> undoStack_;
    std::vector<EditorState> redoStack_;

    int      selectedIndex_ = -1;
    Tool     currentTool_   = Tool::Arrow;
    COLORREF currentColour_ = RGB(52, 199, 89);
    double   currentLineWidth_ = 4.0;

    DragMode   dragMode_ = DragMode::None;
    Handle     activeHandle_ = Handle::None;
    RectD      resizeOriginalRect_{};
    PointD     dragLastPoint_{};
    // Where the drag began, and whether it has travelled far enough to
    // count. Without a threshold, the stray mouse-move between the two
    // clicks of a double-click moves the mark a pixel and banks an undo
    // step for it — and every click-to-select risks the same.
    PointD     dragAnchor_{};
    bool       dragPassedThreshold_ = false;
    // Where in the label the user grabbed it, so it swings from that point
    // instead of teleporting its centre under the pointer.
    PointD     labelGrabOffset_{};
    bool       needsSnapshotBeforeDrag_ = false;
    bool       hasDraft_ = false;
    Annotation draft_;

    ULONGLONG lastStyleChangeAt_ = 0;
    // Nudging coalesces its undo steps too, but on its own clock. Sharing
    // lastStyleChangeAt_ meant a restyle and a nudge within half a second
    // of each other folded into one undo step — and so did nudging one
    // mark, selecting another, and nudging that.
    ULONGLONG lastNudgeAt_    = 0;
    int       lastNudgeIndex_ = -1;
    // Set between mouse-down and mouse-up on the slider. Without it, moving
    // the pointer across the slider on the way somewhere else would drag it.
    bool      draggingSlider_ = false;

    PointD       textAnchor_{};
    COLORREF     textEntryColour_ = RGB(52, 199, 89);
    bool         textEntryActive_ = false;
    // While a label is being typed, the index of the mark it belongs to.
    // -1 means the entry is a standalone Text mark, which was the only case
    // before labels existed.
    int          labelIndex_      = -1;
    // The label's own text, taken off the mark for the duration of the edit
    // so the committed label is not drawn underneath the field showing the
    // same words. Put back if the edit is cancelled.
    std::wstring labelBeingEdited_;
    ScopedFont   textFont_;
    WNDPROC      textEditOriginalProc_ = nullptr;

    ULONGLONG    popupClosedAt_ = 0;
    std::wstring baseTitle_;
};
