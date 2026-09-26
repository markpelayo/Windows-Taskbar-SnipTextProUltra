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

    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;
    ~EditorWindow();

    HWND Window() const { return hwnd_; }

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

    void LayoutChildren();
    void PaintCanvas(HDC dc);
    void RefreshToolbarState();
    void ReturnFocusToCanvas();

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
    ScopedFont   textFont_;
    WNDPROC      textEditOriginalProc_ = nullptr;

    ULONGLONG    popupClosedAt_ = 0;
    std::wstring baseTitle_;
};
