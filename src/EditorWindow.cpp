#include "EditorWindow.h"

#include "EditorSettings.h"
#include "Log.h"
#include "MediaFolder.h"
#include "Util.h"

#include <commctrl.h>
#include <dwmapi.h>

// See the note in Bitmap.cpp: gdiplustypes.h needs min/max, and this project
// builds with NOMINMAX.
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <objidl.h>
#include <gdiplus.h>

// Named imports rather than a using-directive, because Gdiplus::Bitmap would
// otherwise make every unqualified `Bitmap` in this file ambiguous with the
// project's own image type.
using Gdiplus::Color;
using Gdiplus::Font;
using Gdiplus::Graphics;
using Gdiplus::Pen;
using Gdiplus::REAL;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;
using Gdiplus::SmoothingModeAntiAlias;
using Gdiplus::TextRenderingHintAntiAliasGridFit;

namespace {

constexpr const wchar_t* kFrameClass  = L"SnipTextEditorFrame";
constexpr const wchar_t* kCanvasClass = L"SnipTextEditorCanvas";
constexpr const wchar_t* kPopupClass  = L"SnipTextColourPopup";

constexpr int IDC_UNDO       = 101;
constexpr int IDC_REDO       = 102;
constexpr int IDC_COPY       = 103;
constexpr int IDC_SAVE       = 104;
constexpr int IDC_SWATCH     = 105;
constexpr int IDC_SLIDER     = 106;
constexpr int IDC_TOOL_FIRST = 110;
constexpr int IDC_TEXTEDIT   = 120;

constexpr UINT_PTR kTitleFlashTimer = 1;
constexpr UINT     kTitleFlashMs    = 1200;

// Posted to the frame so the system colour picker opens on a later
// message-loop turn, after the popup that requested it has finished
// destroying itself.
constexpr UINT WM_OPEN_COLOUR_PICKER = WM_APP + 1;

constexpr int kBarHeight     = 44;
constexpr int kBarPadding    = 10;
constexpr int kButtonWidth   = 88;
constexpr int kButtonHeight  = 28;
constexpr int kToolWidth     = 62;
constexpr int kSwatchWidth   = 52;
constexpr int kSliderWidth   = 140;

// The narrowest the window may be without clipping a toolbar button off the
// right edge. Derived, because a literal here is a number that has to be
// remembered every time a button is added — and is not, which is how the Lift
// button went missing on small captures: the minimum size was raised for
// resizing but the CREATION size still had its own copy of the old literal,
// so any capture small enough to hit the floor opened one button short.
//
// The bottom bar is the widest row: padding, swatch, slider, then one button
// per tool, each followed by a 6px gap.
constexpr int kMinContentWidth = kBarPadding
                               + kSwatchWidth + 6
                               + kSliderWidth + 6
                               + kToolCount * (kToolWidth + 6)
                               + kBarPadding;

// The whole client area, bars included — the same thing the width constant
// means. Defining it as the CANVAS height instead is what let the two floors
// disagree: creation added the two bars on top of 380 and so floored the
// client at 468, while the resize minimum used 380 as the entire client and
// left the canvas 292px.
constexpr int kMinCanvasHeight  = 380;
constexpr int kMinContentHeight = kMinCanvasHeight + kBarHeight * 2;

// Chrome is drawn in view units, not image units, so handles stay a usable
// size on a canvas that has been scaled down.
constexpr int    kHandleSize    = 9;
constexpr double kHitTolerance  = 8.0;    // view points, converted to image space
constexpr double kMinimumDrag   = 3.0;    // ditto
constexpr double kPenPointGap   = 1.5;    // ditto
constexpr size_t kUndoCap       = 50;
constexpr ULONGLONG kStyleCoalesceMs = 500;
constexpr ULONGLONG kPopupReopenGuardMs = 250;

constexpr int kSliderMin = 1;
constexpr int kSliderMax = 40;

// Colour grid geometry.
constexpr int kGridColumns = 5;
constexpr int kCellSize    = 32;
constexpr int kCellGap     = 6;
constexpr int kGridPadding = 10;

COLORREF AccentColour() {
    // Falls back to the Windows 11 default accent if DWM has nothing to say.
    DWORD colour = 0;
    BOOL  opaque = FALSE;
    if (SUCCEEDED(::DwmGetColorizationColor(&colour, &opaque))) {
        return RGB((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF);
    }
    return RGB(0, 120, 215);
}

HFONT UiFont() {
    static HFONT font = nullptr;
    if (!font) {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            font = ::CreateFontIndirectW(&metrics.lfMessageFont);
        }
    }
    return font ? font : static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
}

HWND MakeButton(HWND parent, const wchar_t* text, int id, DWORD extraStyle = 0) {
    HWND button = ::CreateWindowExW(0, L"BUTTON", text,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | extraStyle,
                                    0, 0, 10, 10, parent,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    ::GetModuleHandleW(nullptr), nullptr);
    if (button) ::SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
    return button;
}

} // namespace

// ---------------------------------------------------------------------------

EditorWindow* EditorWindow::Open(std::unique_ptr<Bitmap> image, CloseCallback onClose) {
    if (!image || !image->IsValid()) return nullptr;

    auto* editor = new EditorWindow(std::move(image), std::move(onClose));
    if (!editor->Create()) {
        delete editor;
        return nullptr;
    }
    return editor;
}

EditorWindow::EditorWindow(std::unique_ptr<Bitmap> image, CloseCallback onClose)
    : image_(std::move(image)), onClose_(std::move(onClose)) {
    currentTool_      = editor_settings::CurrentTool();
    currentColour_    = editor_settings::Colour();
    currentLineWidth_ = editor_settings::LineWidth();
    textEntryColour_  = currentColour_;
}

EditorWindow::~EditorWindow() = default;

// --- window creation -------------------------------------------------------

bool EditorWindow::Create() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW frame{};
        frame.cbSize        = sizeof(frame);
        frame.lpfnWndProc   = &EditorWindow::FrameProc;
        frame.hInstance     = ::GetModuleHandleW(nullptr);
        frame.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        frame.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        frame.lpszClassName = kFrameClass;
        frame.hIcon         = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        ::RegisterClassExW(&frame);

        WNDCLASSEXW canvas{};
        canvas.cbSize        = sizeof(canvas);
        canvas.style         = CS_HREDRAW | CS_VREDRAW;
        canvas.lpfnWndProc   = &EditorWindow::CanvasProc;
        canvas.hInstance     = ::GetModuleHandleW(nullptr);
        canvas.hCursor       = ::LoadCursorW(nullptr, IDC_CROSS);
        canvas.hbrBackground = nullptr;
        canvas.lpszClassName = kCanvasClass;
        ::RegisterClassExW(&canvas);

        WNDCLASSEXW popup{};
        popup.cbSize        = sizeof(popup);
        popup.lpfnWndProc   = &EditorWindow::SwatchProc;
        popup.hInstance     = ::GetModuleHandleW(nullptr);
        popup.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        popup.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        popup.lpszClassName = kPopupClass;
        ::RegisterClassExW(&popup);

        registered = true;
    }

    baseTitle_ = util::Format(L"Screenshot %d × %d", image_->Width(), image_->Height());

    // Fit into 80% of the work area, leaving room for both bars, then apply a
    // floor so the toolbars are never squeezed out of usable shape.
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int availableWidth  = (std::max)(640, util::RectWidth(work));
    const int availableHeight = (std::max)(480, util::RectHeight(work));

    const double fit = (std::min)(1.0,
        (std::min)(availableWidth * 0.8 / (std::max)(1, image_->Width()),
                   (availableHeight * 0.8 - 160) / (std::max)(1, image_->Height())));

    // Both floors come from the shared constants, so the size the window
    // OPENS at and the size it can be RESIZED to agree by construction. They
    // did not before, and the creation path was the one with the stale number.
    const int contentWidth  = (std::max)(kMinContentWidth,
                                         static_cast<int>(image_->Width() * fit));
    const int contentHeight = (std::max)(kMinContentHeight,
                                         static_cast<int>(image_->Height() * fit)
                                             + kBarHeight * 2);

    // ...ForDpi, not the plain one. AdjustWindowRectEx reports non-client
    // metrics at 96 DPI regardless of the actual display, and this process is
    // Per-Monitor-V2 — so on a 150% monitor the real caption and border are
    // larger than it claims and the client area comes out short. There is no
    // window yet to ask about, so this is the system DPI; WM_GETMINMAXINFO
    // below asks the window itself once there is one.
    RECT frameRect{ 0, 0, contentWidth, contentHeight };
    ::AdjustWindowRectExForDpi(&frameRect, WS_OVERLAPPEDWINDOW, FALSE, 0,
                               ::GetDpiForSystem());

    hwnd_ = ::CreateWindowExW(
        0, kFrameClass, baseTitle_.c_str(), WS_OVERLAPPEDWINDOW,
        work.left + (util::RectWidth(work) - util::RectWidth(frameRect)) / 2,
        work.top + (util::RectHeight(work) - util::RectHeight(frameRect)) / 2,
        util::RectWidth(frameRect), util::RectHeight(frameRect),
        nullptr, nullptr, ::GetModuleHandleW(nullptr), this);

    if (!hwnd_) {
        logging::Write(L"editor: couldn't create the window");
        return false;
    }

    ::ShowWindow(hwnd_, SW_SHOW);
    ::SetForegroundWindow(hwnd_);
    ReturnFocusToCanvas();
    return true;
}

// --- message routing -------------------------------------------------------

LRESULT CALLBACK EditorWindow::FrameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    EditorWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<EditorWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<EditorWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    return self->OnFrameMessage(message, wParam, lParam);
}

LRESULT CALLBACK EditorWindow::CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<EditorWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<EditorWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    // The canvas member is not assigned until CreateWindowEx returns, so the
    // handle has to come from the message rather than from the object.
    if (!self->canvas_) self->canvas_ = hwnd;
    return self->OnCanvasMessage(message, wParam, lParam);
}

LRESULT CALLBACK EditorWindow::SwatchProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<EditorWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<EditorWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    return self->OnSwatchMessage(hwnd, message, wParam, lParam);
}

// --- frame -----------------------------------------------------------------

LRESULT EditorWindow::OnFrameMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        undoButton_ = MakeButton(hwnd_, L"Undo", IDC_UNDO);
        redoButton_ = MakeButton(hwnd_, L"Redo", IDC_REDO);
        copyButton_ = MakeButton(hwnd_, L"Copy", IDC_COPY);
        saveButton_ = MakeButton(hwnd_, L"Save…", IDC_SAVE, BS_DEFPUSHBUTTON);

        swatch_ = MakeButton(hwnd_, L"", IDC_SWATCH, BS_OWNERDRAW);

        ::InitCommonControls();
        slider_ = ::CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                    WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                    0, 0, 10, 10, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SLIDER)),
                                    ::GetModuleHandleW(nullptr), nullptr);
        if (slider_) {
            ::SendMessageW(slider_, TBM_SETRANGE, TRUE, MAKELPARAM(kSliderMin, kSliderMax));
            ::SendMessageW(slider_, TBM_SETPOS, TRUE,
                           static_cast<LPARAM>(static_cast<int>(currentLineWidth_)));
        }

        for (int i = 0; i < kToolCount; ++i) {
            Tool tool = static_cast<Tool>(i);
            toolButtons_[i] = MakeButton(hwnd_, ToolTitle(tool), IDC_TOOL_FIRST + i,
                                         BS_AUTOCHECKBOX | BS_PUSHLIKE);
        }

        canvas_ = ::CreateWindowExW(0, kCanvasClass, L"", WS_CHILD | WS_VISIBLE,
                                    0, 0, 10, 10, hwnd_, nullptr,
                                    ::GetModuleHandleW(nullptr), this);

        LayoutChildren();
        RefreshToolbarState();
        return 0;
    }

    case WM_SIZE:
        LayoutChildren();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);

        // Same two constants the creation path uses — but converted from a
        // CLIENT size to a FRAME size first, which is the units this message
        // is in. Assigning the client minimum directly would leave the border
        // and title bar unaccounted for, so the client area could still be
        // squeezed about sixteen pixels under the minimum and clip the last
        // tool button after all: the same bug, quieter.
        RECT frame{ 0, 0, kMinContentWidth, kMinContentHeight };
        ::AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                   ::GetDpiForWindow(hwnd));
        info->ptMinTrackSize.x = util::RectWidth(frame);
        info->ptMinTrackSize.y = util::RectHeight(frame);
        return 0;
    }

    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID != IDC_SWATCH) break;

        RECT box = item->rcItem;
        ::InflateRect(&box, -2, -3);
        ScopedBrush fill(::CreateSolidBrush(currentColour_));
        if (fill) ::FillRect(item->hDC, &box, fill.get());
        // A white swatch needs the outline to be visible at all against the
        // toolbar behind it.
        ScopedPen border(::CreatePen(PS_SOLID, 1, RGB(128, 128, 128)));
        if (border) {
            SelectGuard penGuard(item->hDC, border.get());
            SelectGuard brushGuard(item->hDC, ::GetStockObject(NULL_BRUSH));
            ::Rectangle(item->hDC, box.left, box.top, box.right, box.bottom);
        }
        return TRUE;
    }

    case WM_HSCROLL: {
        if (reinterpret_cast<HWND>(lParam) != slider_) break;
        const int position = static_cast<int>(::SendMessageW(slider_, TBM_GETPOS, 0, 0));
        SetCurrentLineWidth(static_cast<double>(position));
        // Focus must come back or Delete and Esc silently stop working on
        // the selection.
        ReturnFocusToCanvas();
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id >= IDC_TOOL_FIRST && id < IDC_TOOL_FIRST + kToolCount) {
            CommitTextEntry();
            SetCurrentTool(static_cast<Tool>(id - IDC_TOOL_FIRST));
            RefreshToolbarState();
            // Repainted because the canvas now shows something that depends
            // on the selected tool — the Lift hint. Before that, changing
            // tools changed nothing on the canvas and this was unnecessary.
            ::InvalidateRect(canvas_, nullptr, FALSE);
            ReturnFocusToCanvas();
            return 0;
        }
        switch (id) {
        case IDC_UNDO:   Undo(); ReturnFocusToCanvas(); return 0;
        case IDC_REDO:   Redo(); ReturnFocusToCanvas(); return 0;
        case IDC_COPY:   CopyToClipboard(); ReturnFocusToCanvas(); return 0;
        case IDC_SAVE:   SaveAsPng(); ReturnFocusToCanvas(); return 0;
        case IDC_SWATCH: ShowColourPopup(); return 0;
        case IDC_TEXTEDIT:
            if (HIWORD(wParam) == EN_KILLFOCUS) CommitTextEntry();
            return 0;
        }
        break;
    }

    case WM_OPEN_COLOUR_PICKER:
        OpenSystemColourPicker();
        return 0;

    case WM_TIMER:
        if (wParam == kTitleFlashTimer) {
            ::KillTimer(hwnd_, kTitleFlashTimer);
            // The base title is captured once, at construction. Reading the
            // current title here would let a second flash inside the revert
            // window latch "Copied" permanently.
            ::SetWindowTextW(hwnd_, baseTitle_.c_str());
        }
        return 0;

    case WM_CLOSE:
        CancelTextEntry();
        ::DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        HideColourPopup();
        if (onClose_) onClose_(this);
        return 0;
    }
    return ::DefWindowProcW(hwnd_, message, wParam, lParam);
}

void EditorWindow::LayoutChildren() {
    if (!hwnd_) return;
    RECT client{};
    ::GetClientRect(hwnd_, &client);
    const int width  = util::RectWidth(client);
    const int height = util::RectHeight(client);

    const int topY = (kBarHeight - kButtonHeight) / 2;
    int x = kBarPadding;
    auto place = [&](HWND control, int w, int h, int y) {
        if (control) ::MoveWindow(control, x, y, w, h, TRUE);
        x += w + 6;
    };

    place(undoButton_, kButtonWidth, kButtonHeight, topY);
    place(redoButton_, kButtonWidth, kButtonHeight, topY);

    // Copy and Save are right-aligned; everything else grows from the left.
    int rightX = width - kBarPadding - kButtonWidth;
    if (saveButton_) ::MoveWindow(saveButton_, rightX, topY, kButtonWidth, kButtonHeight, TRUE);
    rightX -= kButtonWidth + 6;
    if (copyButton_) ::MoveWindow(copyButton_, rightX, topY, kButtonWidth, kButtonHeight, TRUE);

    const int bottomY = height - kBarHeight + (kBarHeight - kButtonHeight) / 2;
    x = kBarPadding;
    place(swatch_, kSwatchWidth, kButtonHeight, bottomY);
    place(slider_, kSliderWidth, kButtonHeight, bottomY);
    for (HWND button : toolButtons_) place(button, kToolWidth, kButtonHeight, bottomY);

    if (canvas_) {
        ::MoveWindow(canvas_, 0, kBarHeight, width,
                     (std::max)(1, height - kBarHeight * 2), TRUE);
    }
}

void EditorWindow::RefreshToolbarState() {
    if (undoButton_) ::EnableWindow(undoButton_, !undoStack_.empty());
    if (redoButton_) ::EnableWindow(redoButton_, !redoStack_.empty());
    for (int i = 0; i < kToolCount; ++i) {
        if (!toolButtons_[i]) continue;
        ::SendMessageW(toolButtons_[i], BM_SETCHECK,
                       (static_cast<Tool>(i) == currentTool_) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (swatch_) ::InvalidateRect(swatch_, nullptr, TRUE);
}

void EditorWindow::ReturnFocusToCanvas() {
    if (canvas_ && !textEntryActive_) ::SetFocus(canvas_);
}

// --- coordinate mapping ----------------------------------------------------

double EditorWindow::ImageScale() const {
    if (!canvas_ || !image_) return 1.0;
    RECT client{};
    ::GetClientRect(canvas_, &client);
    const double scale = (std::min)(1.0,
        (std::min)(static_cast<double>(util::RectWidth(client)) / (std::max)(1, image_->Width()),
                   static_cast<double>(util::RectHeight(client)) / (std::max)(1, image_->Height())));
    return (std::max)(scale, 0.0001);
}

RECT EditorWindow::ImageRect() const {
    RECT client{};
    if (canvas_) ::GetClientRect(canvas_, &client);
    const double scale = ImageScale();
    const int width  = static_cast<int>(image_->Width() * scale);
    const int height = static_cast<int>(image_->Height() * scale);
    const int left = (util::RectWidth(client) - width) / 2;
    const int top  = (util::RectHeight(client) - height) / 2;
    return util::MakeRect(left, top, left + width, top + height);
}

PointD EditorWindow::ToImagePoint(POINT view) const {
    const RECT   rect  = ImageRect();
    const double scale = ImageScale();
    return PointD{ (view.x - rect.left) / scale, (view.y - rect.top) / scale };
}

POINT EditorWindow::ToViewPoint(PointD image) const {
    const RECT   rect  = ImageRect();
    const double scale = ImageScale();
    POINT out{ rect.left + static_cast<int>(image.x * scale),
               rect.top  + static_cast<int>(image.y * scale) };
    return out;
}

// --- canvas ----------------------------------------------------------------

LRESULT EditorWindow::OnCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(canvas_, &paint);
        if (dc) PaintCanvas(dc);
        // EndPaint runs even when the DC came back null: skipping it would
        // leave the update region unvalidated and Windows would send WM_PAINT
        // again immediately, forever.
        ::EndPaint(canvas_, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        // Committing first means an in-progress label is never silently lost
        // by clicking elsewhere.
        CommitTextEntry();
        ::SetFocus(canvas_);
        ::SetCapture(canvas_);

        POINT view{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const PointD point = ToImagePoint(view);
        const double scale = ImageScale();
        const double tolerance = kHitTolerance / scale;

        dragLastPoint_ = point;
        needsSnapshotBeforeDrag_ = true;

        // 1. Handles of the current selection. They sit on the outline and
        //    are small, so if you hit one you meant it.
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
            for (const auto& entry : annotations_[selectedIndex_].Handles()) {
                const POINT handleView = ToViewPoint(entry.second);
                if (std::hypot(view.x - handleView.x, view.y - handleView.y) <= kHandleSize) {
                    activeHandle_       = entry.first;
                    dragMode_           = DragMode::Resizing;
                    resizeOriginalRect_ = annotations_[selectedIndex_].NormalizedRect();
                    return 0;
                }
            }
        }

        // 2. The topmost annotation under the point, searched from the end.
        {
            Graphics measure(canvas_);
            for (int i = static_cast<int>(annotations_.size()) - 1; i >= 0; --i) {
                if (annotations_[i].HitTest(point, tolerance, &measure)) {
                    selectedIndex_ = i;
                    dragMode_      = DragMode::Moving;
                    ::InvalidateRect(canvas_, nullptr, FALSE);
                    return 0;
                }
            }
        }

        // 3. Empty space.
        selectedIndex_ = -1;
        if (currentTool_ == Tool::Text) {
            ::InvalidateRect(canvas_, nullptr, FALSE);
            ::ReleaseCapture();
            BeginTextEntry(point);
            return 0;
        }

        dragMode_ = DragMode::Drawing;
        draft_ = Annotation{};
        draft_.tool      = currentTool_;
        draft_.colour    = currentColour_;
        draft_.lineWidth = currentLineWidth_;
        draft_.start     = point;
        draft_.end       = point;
        draft_.points    = { point };
        hasDraft_ = true;
        ::InvalidateRect(canvas_, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (dragMode_ == DragMode::None) return 0;
        POINT view{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const PointD point = ToImagePoint(view);
        const double scale = ImageScale();

        switch (dragMode_) {
        case DragMode::Drawing:
            draft_.end = point;
            if (draft_.tool == Tool::Pen && !draft_.points.empty()) {
                const PointD& last = draft_.points.back();
                // Skipping near-duplicates stops a slow stroke accumulating
                // tens of thousands of points.
                if (std::hypot(point.x - last.x, point.y - last.y) > kPenPointGap / scale) {
                    draft_.points.push_back(point);
                }
            }
            break;
        case DragMode::Moving:
            TakeDragSnapshotIfNeeded();
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
                annotations_[selectedIndex_].MoveBy(point.x - dragLastPoint_.x,
                                                    point.y - dragLastPoint_.y);
            }
            break;
        case DragMode::Resizing:
            TakeDragSnapshotIfNeeded();
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
                annotations_[selectedIndex_].Resize(activeHandle_, resizeOriginalRect_, point);
            }
            break;
        default:
            break;
        }

        dragLastPoint_ = point;
        ::InvalidateRect(canvas_, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        ::ReleaseCapture();
        POINT view{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const PointD point = ToImagePoint(view);
        const double scale = ImageScale();

        if (dragMode_ != DragMode::Drawing) {
            // A move or resize just ended; its snapshot was taken on the
            // first drag event.
            dragMode_ = DragMode::None;
            needsSnapshotBeforeDrag_ = false;
            RefreshToolbarState();
            ::InvalidateRect(canvas_, nullptr, FALSE);
            return 0;
        }

        Annotation shape = draft_;
        hasDraft_ = false;
        dragMode_ = DragMode::None;
        needsSnapshotBeforeDrag_ = false;
        shape.end = point;

        if (shape.tool == Tool::Pen) {
            // The mouse-up point is always appended, or every stroke ends
            // short of where it was released.
            if (shape.points.empty() ||
                shape.points.back().x != point.x || shape.points.back().y != point.y) {
                shape.points.push_back(point);
            }
        }

        const double dragged = std::hypot(shape.end.x - shape.start.x,
                                          shape.end.y - shape.start.y);
        const bool tooSmall = (shape.tool == Tool::Pen)
                                  ? shape.points.size() < 2
                                  : dragged < kMinimumDrag / scale;
        if (tooSmall) {
            // A click rather than a drag. No shape, and no undo state either.
            ::InvalidateRect(canvas_, nullptr, FALSE);
            return 0;
        }

        if (shape.tool == Tool::Lift) {
            // The drag picked the region; it has not moved anywhere yet. The
            // piece is created sitting exactly on top of where it came from,
            // so the picture looks unchanged until it is dragged away — which
            // is what makes both variants read correctly:
            //
            //   plain drag   the original stays put, and pulling the piece
            //                aside reveals it still there. A copy.
            //   Shift-drag   the source is blanked underneath at the moment
            //                of the lift, hidden by the piece on top of it,
            //                and pulling the piece aside reveals the hole.
            //                A cut.
            //
            // Clamped to the picture: a selection dragged past the edge would
            // otherwise ask GDI+ to read pixels that are not there.
            RectD region = shape.NormalizedRect();
            const double pictureWidth  = static_cast<double>(image_->Width());
            const double pictureHeight = static_cast<double>(image_->Height());
            const double x0 = (std::max)(0.0, (std::min)(region.MinX(), pictureWidth));
            const double y0 = (std::max)(0.0, (std::min)(region.MinY(), pictureHeight));
            const double x1 = (std::max)(0.0, (std::min)(region.MaxX(), pictureWidth));
            const double y1 = (std::max)(0.0, (std::min)(region.MaxY(), pictureHeight));
            if (x1 - x0 < 1.0 || y1 - y0 < 1.0) {
                ::InvalidateRect(canvas_, nullptr, FALSE);
                return 0;
            }
            region = RectD{ x0, y0, x1 - x0, y1 - y0 };

            shape.source = region;
            shape.start  = { region.MinX(), region.MinY() };
            shape.end    = { region.MaxX(), region.MaxY() };

            if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                shape.blankSource = true;
                shape.blankColour = DominantEdgeColour(region);
            }
        }

        Snapshot();
        annotations_.push_back(std::move(shape));
        selectedIndex_ = static_cast<int>(annotations_.size()) - 1;
        RefreshToolbarState();
        ::InvalidateRect(canvas_, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift   = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // Shortcuts live here rather than in an accelerator table precisely
        // so they stand down while the inline text control has focus: when a
        // label is being typed, this window proc never sees the keystroke, so
        // Ctrl+Z edits the text instead of undoing the drawing.
        if (control) {
            switch (wParam) {
            case 'Z': if (shift) Redo(); else Undo(); return 0;
            case 'Y': Redo(); return 0;
            case 'C': CopyToClipboard(); return 0;
            case 'S': SaveAsPng(); return 0;
            }
            return 0;
        }

        switch (wParam) {
        case VK_DELETE:
        case VK_BACK:
            DeleteSelection();
            return 0;
        case VK_ESCAPE:
            // Cancel an in-progress label first; only if there is none does
            // Esc drop the selection.
            if (!CancelTextEntry()) ClearSelection();
            return 0;
        }
        return 0;
    }

    case WM_COMMAND:
        // The inline edit control is a child of the canvas, so its
        // notifications arrive here rather than at the frame.
        if (LOWORD(wParam) == IDC_TEXTEDIT && HIWORD(wParam) == EN_KILLFOCUS) {
            CommitTextEntry();
            return 0;
        }
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
    }
    return ::DefWindowProcW(canvas_, message, wParam, lParam);
}

void EditorWindow::PaintCanvas(HDC dc) {
    RECT client{};
    ::GetClientRect(canvas_, &client);
    const int width  = util::RectWidth(client);
    const int height = util::RectHeight(client);
    if (width <= 0 || height <= 0) return;

    // Paint through an off-screen bitmap. Drawing the image and every
    // annotation straight to the window would flicker on each mouse-move,
    // and mouse-move is exactly when this runs.
    auto buffer = Bitmap::Create(width, height);
    if (!buffer || !buffer->MemoryDC()) return;
    HDC target = buffer->MemoryDC();

    ::FillRect(target, &client, ::GetSysColorBrush(COLOR_APPWORKSPACE));

    const RECT   rect  = ImageRect();
    const double scale = ImageScale();

    if (image_->MemoryDC()) {
        // Low-quality resampling while a drag is in flight; a full-resolution
        // resample on every mouse-move is what makes a canvas feel sluggish.
        ::SetStretchBltMode(target, dragMode_ == DragMode::None ? HALFTONE : COLORONCOLOR);
        ::SetBrushOrgEx(target, 0, 0, nullptr);
        ::StretchBlt(target, rect.left, rect.top, util::RectWidth(rect), util::RectHeight(rect),
                     image_->MemoryDC(), 0, 0, image_->Width(), image_->Height(), SRCCOPY);
    }

    {
        // Declared before the Graphics, and this order is load-bearing: GDI+
        // batches its drawing, so a DrawImage issued here may still be pending
        // when the scope ends. Destruction runs in reverse order of
        // declaration, so a picture declared second would be freed FIRST,
        // leaving the Graphics to flush a read from a destroyed wrapper over
        // image_'s pixels. Declared first, it is destroyed last.
        std::unique_ptr<Gdiplus::Bitmap> picture = PictureForLift();

        Graphics graphics(target);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        const PointD offset{ static_cast<double>(rect.left), static_cast<double>(rect.top) };
        for (const Annotation& annotation : annotations_) {
            annotation.Draw(graphics, scale, offset, picture.get());
        }
        if (hasDraft_) draft_.Draw(graphics, scale, offset, picture.get());

        // Selection chrome, in view units so it stays usable at any zoom.
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
            const Annotation& selected = annotations_[selectedIndex_];
            const COLORREF accent = AccentColour();
            const Color accentColour(255, GetRValue(accent), GetGValue(accent), GetBValue(accent));

            const RectD box = selected.BoundingBox(&graphics);
            const POINT topLeft     = ToViewPoint({ box.MinX(), box.MinY() });
            const POINT bottomRight = ToViewPoint({ box.MaxX(), box.MaxY() });

            Pen outline(accentColour, 1.0f);
            REAL dashes[2] = { 4.0f, 3.0f };
            outline.SetDashPattern(dashes, 2);
            graphics.DrawRectangle(&outline,
                                   static_cast<REAL>(topLeft.x - 4),
                                   static_cast<REAL>(topLeft.y - 4),
                                   static_cast<REAL>(bottomRight.x - topLeft.x + 8),
                                   static_cast<REAL>(bottomRight.y - topLeft.y + 8));

            SolidBrush white(Color(255, 255, 255, 255));
            Pen ring(accentColour, 1.5f);
            for (const auto& entry : selected.Handles()) {
                const POINT handleView = ToViewPoint(entry.second);
                const REAL half = kHandleSize / 2.0f;
                const REAL left = static_cast<REAL>(handleView.x) - half;
                const REAL top  = static_cast<REAL>(handleView.y) - half;
                graphics.FillEllipse(&white, left, top,
                                     static_cast<REAL>(kHandleSize), static_cast<REAL>(kHandleSize));
                graphics.DrawEllipse(&ring, left, top,
                                     static_cast<REAL>(kHandleSize), static_cast<REAL>(kHandleSize));
            }
        }

        // Lift is the only tool with a modifier, and a modifier nobody knows
        // about is a feature that does not exist. Every other tool does one
        // thing and needs no explanation, so rather than a permanent status
        // bar taking space from all seven, the hint appears only while the
        // tool it describes is selected, and only while nothing is being
        // dragged — by the time a drag is under way the choice has been made,
        // and the label would just sit under the cursor.
        //
        // The alternative was a second toolbar button, "Lift" and "Cut". That
        // is more discoverable and costs an eighth button, which the editor's
        // minimum width cannot currently take. This says the same thing for
        // no width at all.
        if (currentTool_ == Tool::Lift && !hasDraft_) {
            const wchar_t* hint = L"Drag to copy a piece  ·  Shift-drag to cut it out";

            Gdiplus::FontFamily family(L"Segoe UI");
            Font font(&family, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            RectF measured;
            graphics.MeasureString(hint, -1, &font,
                                   Gdiplus::PointF(0.0f, 0.0f), &measured);

            const REAL padX = 10.0f, padY = 5.0f;
            const REAL boxWidth  = measured.Width + padX * 2;
            const REAL boxHeight = measured.Height + padY * 2;
            const REAL boxLeft   = (static_cast<REAL>(width) - boxWidth) / 2.0f;
            const REAL boxTop    = static_cast<REAL>(height) - boxHeight - 12.0f;

            SolidBrush backdrop(Color(170, 0, 0, 0));
            graphics.FillRectangle(&backdrop, boxLeft, boxTop, boxWidth, boxHeight);

            SolidBrush ink(Color(255, 255, 255, 255));
            graphics.DrawString(hint, -1, &font,
                                Gdiplus::PointF(boxLeft + padX, boxTop + padY), &ink);
        }
    }

    ::BitBlt(dc, 0, 0, width, height, target, 0, 0, SRCCOPY);
}

// --- editing ---------------------------------------------------------------

void EditorWindow::Snapshot() {
    undoStack_.push_back(annotations_);
    // Nothing else trims these stacks, and an editor can stay open a long
    // time; the cap is what keeps the memory bounded by construction.
    if (undoStack_.size() > kUndoCap) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
    RefreshToolbarState();
}

void EditorWindow::SnapshotStyleChangeIfNeeded() {
    // One undo step per restyle GESTURE, not per slider tick. A continuous
    // control fires every few milliseconds; without this a single slider drag
    // buries the undo stack.
    const ULONGLONG now = ::GetTickCount64();
    if (now - lastStyleChangeAt_ > kStyleCoalesceMs) Snapshot();
    lastStyleChangeAt_ = now;
}

void EditorWindow::TakeDragSnapshotIfNeeded() {
    // Taken on the first actual drag event, not on mouse-down. Clicking
    // around to select things would otherwise stack up identical undo states
    // and make Ctrl+Z appear broken.
    if (!needsSnapshotBeforeDrag_) return;
    needsSnapshotBeforeDrag_ = false;
    Snapshot();
}

void EditorWindow::Undo() {
    // Ctrl+Z while typing means "forget this label" — and it must CANCEL, not
    // commit. Committing would snapshot, which clears the redo stack and
    // silently turns redo into a no-op.
    if (CancelTextEntry()) { ::InvalidateRect(canvas_, nullptr, FALSE); return; }
    if (undoStack_.empty()) return;

    redoStack_.push_back(annotations_);
    annotations_ = std::move(undoStack_.back());
    undoStack_.pop_back();
    // The index may no longer refer to the same mark.
    selectedIndex_ = -1;
    RefreshToolbarState();
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::Redo() {
    if (CancelTextEntry()) { ::InvalidateRect(canvas_, nullptr, FALSE); return; }
    if (redoStack_.empty()) return;

    undoStack_.push_back(annotations_);
    annotations_ = std::move(redoStack_.back());
    redoStack_.pop_back();
    selectedIndex_ = -1;
    RefreshToolbarState();
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::DeleteSelection() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(annotations_.size())) return;
    Snapshot();
    annotations_.erase(annotations_.begin() + selectedIndex_);
    selectedIndex_ = -1;
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::ClearSelection() {
    if (selectedIndex_ < 0) return;
    selectedIndex_ = -1;
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::SetCurrentTool(Tool tool) {
    // The equality guard is what keeps EditorSettings::IsDefault honest: it
    // compares values, so re-picking the tool you already had must not count
    // as a change.
    if (tool == currentTool_) return;
    currentTool_ = tool;
    editor_settings::SetTool(tool);
}

void EditorWindow::SetCurrentColour(COLORREF colour) {
    if (colour == currentColour_) return;
    currentColour_ = colour;
    editor_settings::SetColour(colour);

    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
        SnapshotStyleChangeIfNeeded();
        annotations_[selectedIndex_].colour = colour;
        ::InvalidateRect(canvas_, nullptr, FALSE);
    }
    if (swatch_) ::InvalidateRect(swatch_, nullptr, TRUE);
}

void EditorWindow::SetCurrentLineWidth(double width) {
    if (std::fabs(width - currentLineWidth_) < 0.001) return;
    currentLineWidth_ = width;
    editor_settings::SetLineWidth(width);

    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
        SnapshotStyleChangeIfNeeded();
        annotations_[selectedIndex_].lineWidth = width;
        ::InvalidateRect(canvas_, nullptr, FALSE);
    }
}

// --- text entry ------------------------------------------------------------

void EditorWindow::BeginTextEntry(PointD anchor) {
    CommitTextEntry();

    const double scale    = ImageScale();
    const double fontSize = (std::max)(14.0, currentLineWidth_ * 5.0);
    const POINT  origin   = ToViewPoint(anchor);

    RECT canvasRect{};
    ::GetClientRect(canvas_, &canvasRect);
    const int width  = (std::min)(320,
        (std::max)(160, static_cast<int>(util::RectWidth(canvasRect) - origin.x - 8)));
    // The field's height is exactly the committed annotation's box height,
    // and its origin is exactly where the glyphs will be drawn. If one of the
    // three changes, all three must, or the text jumps on commit.
    const int height = static_cast<int>(Annotation::TextBoxHeight(fontSize) * scale);

    textEdit_ = ::CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  origin.x, origin.y, width, (std::max)(18, height),
                                  canvas_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTEDIT)),
                                  ::GetModuleHandleW(nullptr), nullptr);
    if (!textEdit_) return;

    LOGFONTW description{};
    description.lfHeight = -static_cast<LONG>(fontSize * scale);
    description.lfWeight = FW_SEMIBOLD;
    ::wcscpy_s(description.lfFaceName, L"Segoe UI");
    // Owned by the editor, replaced on each entry and released with the
    // window, so a long session cannot accumulate font handles.
    textFont_.reset(::CreateFontIndirectW(&description));
    if (textFont_) {
        ::SendMessageW(textEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(textFont_.get()), TRUE);
    }

    // Subclassed so Return commits and Esc cancels. Without this the control
    // beeps at Return, and Esc reaches the control's default handling, which
    // ends editing — and ending editing COMMITS the very label the user was
    // trying to throw away.
    ::SetWindowLongPtrW(textEdit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    textEditOriginalProc_ = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
        textEdit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&EditorWindow::TextEditProc)));

    textAnchor_      = anchor;
    // The colour is captured HERE, not read at commit. Otherwise touching
    // the swatch mid-typing commits the label in a colour it was never shown
    // in.
    textEntryColour_ = currentColour_;
    textEntryActive_ = true;
    ::SetFocus(textEdit_);
}

void EditorWindow::CommitTextEntry() {
    if (!textEntryActive_ || !textEdit_) return;

    // Cleared first, so the re-entrant call that arrives from EN_KILLFOCUS
    // below is a harmless no-op.
    textEntryActive_ = false;
    HWND field = textEdit_;
    textEdit_ = nullptr;

    const int length = ::GetWindowTextLengthW(field);
    std::wstring value;
    if (length > 0) {
        // One extra element for the terminator GetWindowText always writes.
        value.resize(static_cast<size_t>(length) + 1, L'\0');
        const int copied = ::GetWindowTextW(field, &value[0], length + 1);
        value.resize(static_cast<size_t>((std::max)(0, copied)));
    }

    ::SetFocus(canvas_);
    ::DestroyWindow(field);

    // Trim; a label of nothing but spaces is not a label.
    size_t first = value.find_first_not_of(L" \t\r\n");
    size_t last  = value.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return;   // no snapshot, no annotation
    value = value.substr(first, last - first + 1);

    Snapshot();
    Annotation label;
    label.tool      = Tool::Text;
    label.colour    = textEntryColour_;
    label.lineWidth = currentLineWidth_;
    label.start     = textAnchor_;
    label.end       = textAnchor_;
    label.text      = std::move(value);
    annotations_.push_back(std::move(label));
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

bool EditorWindow::CancelTextEntry() {
    if (!textEntryActive_ || !textEdit_) return false;

    textEntryActive_ = false;
    HWND field = textEdit_;
    textEdit_ = nullptr;
    ::SetFocus(canvas_);
    ::DestroyWindow(field);
    // No snapshot, no annotation. The boolean is what lets Esc, undo and redo
    // distinguish "cancelled a label" from "do the normal thing".
    return true;
}

// --- colour popup ----------------------------------------------------------

void EditorWindow::ShowColourPopup() {
    // The popup dismisses itself on deactivation, so by the time the swatch
    // click arrives it is already gone and a naive toggle would immediately
    // reopen it — making the swatch look inert.
    if (::GetTickCount64() - popupClosedAt_ < kPopupReopenGuardMs) return;
    if (colourPopup_) { HideColourPopup(); return; }

    constexpr int rows  = 2;
    const int width  = kGridPadding * 2 + kGridColumns * kCellSize + (kGridColumns - 1) * kCellGap;
    const int height = kGridPadding * 2 + rows * kCellSize + (rows - 1) * kCellGap;

    RECT swatchRect{};
    ::GetWindowRect(swatch_, &swatchRect);

    colourPopup_ = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPopupClass, L"",
                                     WS_POPUP | WS_BORDER,
                                     swatchRect.left, swatchRect.top - height - 4,
                                     width, height, hwnd_, nullptr,
                                     ::GetModuleHandleW(nullptr), this);
    if (!colourPopup_) return;
    ::ShowWindow(colourPopup_, SW_SHOWNA);
    ::SetForegroundWindow(colourPopup_);
}

void EditorWindow::HideColourPopup() {
    if (!colourPopup_) return;
    HWND popup = colourPopup_;
    colourPopup_ = nullptr;
    popupClosedAt_ = ::GetTickCount64();
    ::DestroyWindow(popup);
}

LRESULT EditorWindow::OnSwatchMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(hwnd, &paint);
        if (!dc) { ::EndPaint(hwnd, &paint); return 0; }

        const COLORREF accent = AccentColour();
        for (int index = 0; index < 10; ++index) {
            const int row = index / kGridColumns;
            const int col = index % kGridColumns;
            RECT cell{};
            cell.left   = kGridPadding + col * (kCellSize + kCellGap);
            cell.top    = kGridPadding + row * (kCellSize + kCellGap);
            cell.right  = cell.left + kCellSize;
            cell.bottom = cell.top + kCellSize;

            if (index < 9) {
                ScopedBrush fill(::CreateSolidBrush(editor_settings::kPresetColours[index]));
                if (fill) ::FillRect(dc, &cell, fill.get());
            } else {
                // The tenth cell opens the full system colour picker. Six
                // wedges make it read as a colour wheel without needing an
                // image resource.
                static const COLORREF wedges[6] = {
                    RGB(255, 59, 48), RGB(255, 149, 0), RGB(255, 204, 0),
                    RGB(52, 199, 89), RGB(0, 122, 255), RGB(175, 82, 222)
                };
                const int cx = (cell.left + cell.right) / 2;
                const int cy = (cell.top + cell.bottom) / 2;
                const int radius = kCellSize;   // overshoot, so wedges reach the corners
                for (int w = 0; w < 6; ++w) {
                    ScopedBrush brush(::CreateSolidBrush(wedges[w]));
                    if (!brush) continue;
                    SelectGuard brushGuard(dc, brush.get());
                    SelectGuard penGuard(dc, ::GetStockObject(NULL_PEN));
                    const double a0 = w * 60.0 * 3.14159265358979 / 180.0;
                    const double a1 = (w + 1) * 60.0 * 3.14159265358979 / 180.0;
                    ::Pie(dc, cx - radius, cy - radius, cx + radius, cy + radius,
                          cx + static_cast<int>(std::cos(a0) * radius),
                          cy - static_cast<int>(std::sin(a0) * radius),
                          cx + static_cast<int>(std::cos(a1) * radius),
                          cy - static_cast<int>(std::sin(a1) * radius));
                }
            }

            const bool selected = index < 9 &&
                                  editor_settings::kPresetColours[index] == currentColour_;
            ScopedPen border(::CreatePen(PS_SOLID, selected ? 3 : 1,
                                         selected ? accent : RGB(160, 160, 160)));
            if (border) {
                SelectGuard penGuard(dc, border.get());
                SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));
                ::Rectangle(dc, cell.left, cell.top, cell.right, cell.bottom);
            }
        }

        ::EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_LBUTTONUP: {
        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        for (int index = 0; index < 10; ++index) {
            const int row = index / kGridColumns;
            const int col = index % kGridColumns;
            RECT cell{};
            cell.left   = kGridPadding + col * (kCellSize + kCellGap);
            cell.top    = kGridPadding + row * (kCellSize + kCellGap);
            cell.right  = cell.left + kCellSize;
            cell.bottom = cell.top + kCellSize;
            if (!util::RectContains(cell, point)) continue;

            if (index < 9) {
                SetCurrentColour(editor_settings::kPresetColours[index]);
                HideColourPopup();
            } else {
                // Deferred, for two reasons: this runs inside the popup's own
                // window procedure, which HideColourPopup is about to
                // destroy; and opening a modal dialog during a popup's
                // dismissal hands focus back to the editor and leaves the
                // dialog behind it.
                HWND frame = hwnd_;
                HideColourPopup();
                ::PostMessageW(frame, WM_OPEN_COLOUR_PICKER, 0, 0);
                return 0;
            }
            ReturnFocusToCanvas();
            return 0;
        }
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) HideColourPopup();
        return 0;

    case WM_KILLFOCUS:
        HideColourPopup();
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK EditorWindow::TextEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<EditorWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self || !self->textEditOriginalProc_) return ::DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_GETDLGCODE:
        // Claim Return and Escape so the dialog manager does not eat them.
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) { self->CommitTextEntry(); return 0; }
        if (wParam == VK_ESCAPE) { self->CancelTextEntry(); return 0; }
        break;
    case WM_CHAR:
        // Swallow the beep the control would otherwise make for these.
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0;
        break;
    }
    return ::CallWindowProcW(self->textEditOriginalProc_, hwnd, message, wParam, lParam);
}

void EditorWindow::OpenSystemColourPicker() {
    static COLORREF custom[16] = {};
    CHOOSECOLORW choose{};
    choose.lStructSize  = sizeof(choose);
    choose.hwndOwner    = hwnd_;
    choose.rgbResult    = currentColour_;
    choose.lpCustColors = custom;
    choose.Flags        = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    if (::ChooseColorW(&choose)) SetCurrentColour(choose.rgbResult);
    ReturnFocusToCanvas();
}

// --- export ----------------------------------------------------------------

std::unique_ptr<Bitmap> EditorWindow::Flatten() {
    // An in-progress label must not be silently lost by pressing Copy.
    CommitTextEntry();

    auto output = Bitmap::Create(image_->Width(), image_->Height());
    if (!output || !output->MemoryDC() || !image_->MemoryDC()) return nullptr;

    ::BitBlt(output->MemoryDC(), 0, 0, image_->Width(), image_->Height(),
             image_->MemoryDC(), 0, 0, SRCCOPY);
    output->MakeOpaque();

    {
        // Scoped, so GDI+ has flushed its batched output into the DIB before
        // MakeOpaque below writes the alpha bytes directly. Leaving the
        // Graphics alive across that would be two writers racing over one
        // buffer.
        // Before the Graphics, for the lifetime reason spelled out in
        // PaintCanvas: GDI+ batches, so the view has to outlive the surface
        // that reads from it.
        std::unique_ptr<Gdiplus::Bitmap> picture = PictureForLift();

        Graphics graphics(output->MemoryDC());
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

        // Scale 1, offset 0, and the identical drawing code the canvas uses.
        // That is the whole payoff of storing annotations in image
        // coordinates: the exported file is full resolution and matches what
        // was on screen.
        // The Lift source is read from the ORIGINAL capture, which `output`
        // is a copy of — not from `output` itself. Reading from the surface
        // being drawn into would make each lift see the results of the ones
        // before it, so two overlapping lifts would compound instead of both
        // showing the untouched picture.
        for (const Annotation& annotation : annotations_) {
            annotation.Draw(graphics, 1.0, PointD{ 0.0, 0.0 }, picture.get());
        }
        // The draft is deliberately excluded: a shape still under the mouse
        // has not been committed.
    }

    output->MakeOpaque();
    return output;
}

std::unique_ptr<Gdiplus::Bitmap> EditorWindow::PictureForLift() const {
    if (!image_ || !image_->Bits()) return nullptr;

    // The house rule, the same one Ocr.cpp follows: GDI batches too, and the
    // last thing to write these bits was a BitBlt. Read them without flushing
    // and you can get the buffer as it was before that blt landed.
    ::GdiFlush();

    // Built only when something actually needs it. Every other tool draws its
    // own ink, so on a picture with no lifts in it this costs one loop over a
    // handful of marks and nothing else.
    bool needed = false;
    for (const Annotation& annotation : annotations_) {
        if (annotation.tool == Tool::Lift) { needed = true; break; }
    }
    if (!needed && !(hasDraft_ && draft_.tool == Tool::Lift)) return nullptr;

    // Wraps the DIB's own pixels — the constructor taking a scan0 does not
    // copy. So this is a view, not a second image, and it stays valid only as
    // long as image_ does, which is why it is never stored.
    //
    // The stride is positive because our DIB sections are top-down; a
    // bottom-up DIB would need a negative stride and a pointer to the last
    // row, and would silently draw upside down without it.
    return std::make_unique<Gdiplus::Bitmap>(
        image_->Width(), image_->Height(), image_->Stride(),
        PixelFormat32bppRGB,
        static_cast<BYTE*>(image_->Bits()));
}

COLORREF EditorWindow::DominantEdgeColour(const RectD& region) const {
    if (!image_ || !image_->Bits()) return RGB(255, 255, 255);
    ::GdiFlush();   // as above: these are raw DIB bits GDI last wrote to

    const int width  = image_->Width();
    const int height = image_->Height();
    const BYTE* pixels = static_cast<const BYTE*>(image_->Bits());
    const int stride = image_->Stride();

    const int left   = static_cast<int>(std::floor(region.MinX()));
    const int top    = static_cast<int>(std::floor(region.MinY()));
    // Inclusive last column and row. ceil(MaxX) is one PAST the region, so
    // without the -1 the right and bottom edges would be sampled one pixel
    // further out than the left and top, and the "two-pixel ring" below would
    // be lopsided — wrong pixels on exactly the gradient backgrounds where
    // the sampled colour has to be right.
    const int right  = static_cast<int>(std::ceil(region.MaxX())) - 1;
    const int bottom = static_cast<int>(std::ceil(region.MaxY())) - 1;

    // The mode, not the mean. Averaging a border that is mostly white with a
    // few dark pixels of text gives a grey that matches nothing on screen;
    // the most common colour gives the actual background, and a stray dark
    // pixel cannot outvote it.
    //
    // Colours are bucketed to 5 bits per channel first. Screenshots are full
    // of near-identical shades from antialiasing and subpixel rendering, and
    // counting exact values would split one background across a dozen entries
    // and let a rarer exact match win.
    std::unordered_map<unsigned int, int> counts;
    int best = -1;
    COLORREF winner = RGB(255, 255, 255);

    auto sample = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const BYTE* p = pixels + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
        const BYTE b = p[0], g = p[1], r = p[2];
        const unsigned int key = (static_cast<unsigned int>(r >> 3) << 10)
                               | (static_cast<unsigned int>(g >> 3) << 5)
                               |  static_cast<unsigned int>(b >> 3);
        const int count = ++counts[key];
        if (count > best) { best = count; winner = RGB(r, g, b); }
    };

    // A two-pixel ring just outside the region.
    for (int offset = 1; offset <= 2; ++offset) {
        for (int x = left - offset; x <= right + offset; ++x) {
            sample(x, top - offset);
            sample(x, bottom + offset);
        }
        for (int y = top - offset; y <= bottom + offset; ++y) {
            sample(left - offset, y);
            sample(right + offset, y);
        }
    }

    // Nothing sampled means the region covered the whole picture, edges and
    // all. White is as good a guess as any and better than a crash.
    return best < 0 ? RGB(255, 255, 255) : winner;
}

void EditorWindow::CopyToClipboard() {
    auto flattened = Flatten();
    if (!flattened || !flattened->CopyToClipboard(hwnd_)) {
        ::MessageBeep(MB_ICONWARNING);
        return;
    }
    logging::Write(L"editor: copied the annotated image to the clipboard");
    FlashTitle(L"Copied");
}

void EditorWindow::SaveAsPng() {
    auto flattened = Flatten();
    if (!flattened) { ::MessageBeep(MB_ICONWARNING); return; }

    std::vector<BYTE> png = flattened->EncodePng();
    if (png.empty()) { ::MessageBeep(MB_ICONWARNING); return; }

    MediaFolder& folder = MediaFolder::Screenshots();
    folder.EnsureDirectoryExists();

    std::wstring name = L"SnipText " + util::FileNameTimestamp() + L".png";
    std::vector<wchar_t> buffer(name.begin(), name.end());
    buffer.resize(MAX_PATH, L'\0');

    const std::wstring initialDirectory = folder.Directory();

    OPENFILENAMEW dialog{};
    dialog.lStructSize     = sizeof(dialog);
    dialog.hwndOwner       = hwnd_;
    dialog.lpstrFilter     = L"PNG image\0*.png\0All files\0*.*\0";
    dialog.lpstrFile       = buffer.data();
    dialog.nMaxFile        = MAX_PATH;
    dialog.lpstrDefExt     = L"png";
    // Opens where the menu's "Show Saved Images" points, so everything lands
    // in one place by default.
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!::GetSaveFileNameW(&dialog)) return;   // cancelled: nothing at all happens

    ScopedFile file(::CreateFileW(buffer.data(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        ::MessageBoxW(hwnd_, L"That file couldn't be written.", L"SnipText",
                      MB_OK | MB_ICONWARNING);
        return;
    }

    DWORD written = 0;
    ::WriteFile(file.get(), png.data(), static_cast<DWORD>(png.size()), &written, nullptr);
    logging::Write(util::Format(L"editor: saved %zu bytes to %s", png.size(), buffer.data()));
    FlashTitle(L"Saved");
}

void EditorWindow::FlashTitle(const wchar_t* note) {
    ::KillTimer(hwnd_, kTitleFlashTimer);
    ::SetWindowTextW(hwnd_, note);
    ::SetTimer(hwnd_, kTitleFlashTimer, kTitleFlashMs, nullptr);
}
