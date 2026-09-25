#include "RegionOverlay.h"

#include "Capture.h"
#include "Log.h"
#include "Util.h"
#include "VideoSettings.h"

#include <dwmapi.h>

namespace {

constexpr const wchar_t* kWindowClass = L"SnipTextRegionOverlay";

// Every constant the overlay draws with, in one place.
constexpr int kMinimumSize       = 16;   // also the floor below which Confirm refuses
constexpr int kGripSize          = 10;   // drawn size of a handle
constexpr int kGripHitInflate    = 6;    // grown so the handles are catchable
constexpr int kBorderWidth       = 2;
constexpr int kNewRectDeadZone   = 4;    // a shaky click must not wipe the selection
// Covers the border, the handles and the readout — everything anchored within
// a few pixels of the selection. NOT the hint bar or the Record button, which
// SetSelection invalidates explicitly; see the note there.
constexpr int kInvalidatePadding = 90;
constexpr int kReadoutPadding    = 6;
constexpr int kButtonWidth       = 110;
constexpr int kButtonHeight      = 32;
// Wide enough for the longest hint, which is the window-pick one at about
// 70 characters. The text is also drawn with DT_END_ELLIPSIS, so a wider
// system font trims rather than clipping mid-word at both ends.
constexpr int kHintWidth         = 560;
constexpr int kHintHeight        = 24;

constexpr BYTE kDimAlpha     = 115;   // 0.45 of 255
constexpr BYTE kHintAlpha    = 140;   // 0.55
constexpr BYTE kReadoutAlpha = 178;   // 0.70

bool g_isShowing = false;

// Blends a solid colour over a device context at the given alpha, using a
// single 1x1 source bitmap. AlphaBlend needs a source DC, and creating one
// per paint would be wasteful on a window that repaints on every mouse move.
void FillAlpha(HDC dc, const RECT& rect, COLORREF colour, BYTE alpha) {
    if (util::RectWidth(rect) <= 0 || util::RectHeight(rect) <= 0) return;

    ScopedDC source(::CreateCompatibleDC(dc));
    if (!source) return;

    BITMAPINFO info{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = 1;
    info.bmiHeader.biHeight      = -1;
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    ScopedBitmap pixel(::CreateDIBSection(source.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!pixel || !bits) return;

    // AlphaBlend wants premultiplied components.
    BYTE* rgba = static_cast<BYTE*>(bits);
    rgba[0] = static_cast<BYTE>(GetBValue(colour) * alpha / 255);
    rgba[1] = static_cast<BYTE>(GetGValue(colour) * alpha / 255);
    rgba[2] = static_cast<BYTE>(GetRValue(colour) * alpha / 255);
    rgba[3] = alpha;

    SelectGuard guard(source.get(), pixel.get());
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    // The source alpha is already baked into the pixel, so SourceConstantAlpha
    // stays at 255 and the per-pixel alpha does the work.
    blend.SourceConstantAlpha = 255;
    ::AlphaBlend(dc, rect.left, rect.top, util::RectWidth(rect), util::RectHeight(rect),
                 source.get(), 0, 0, 1, 1, blend);
}

void FillSolid(HDC dc, const RECT& rect, COLORREF colour) {
    ScopedBrush brush(::CreateSolidBrush(colour));
    if (brush) ::FillRect(dc, &rect, brush.get());
}

void StrokeRect(HDC dc, const RECT& rect, COLORREF colour, int width) {
    ScopedPen pen(::CreatePen(PS_SOLID, width, colour));
    if (!pen) return;
    SelectGuard penGuard(dc, pen.get());
    SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));
    // A pen straddles the path, so inset by half the width to keep the whole
    // stroke inside the selection rather than eating into it.
    const int inset = width / 2;
    ::Rectangle(dc, rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset);
}

ScopedFont MakeFont(int height, int weight) {
    LOGFONTW description{};
    description.lfHeight  = -height;
    description.lfWeight  = weight;
    description.lfQuality = CLEARTYPE_QUALITY;
    ::wcscpy_s(description.lfFaceName, L"Segoe UI");
    return ScopedFont(::CreateFontIndirectW(&description));
}

// The true visible bounds of a window, which is not GetWindowRect: since
// Windows 10 that includes the invisible resize border, so a window picked
// this way would come back with a few dead pixels down each side.
RECT VisibleWindowBounds(HWND window) {
    RECT bounds{};
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                       &bounds, sizeof(bounds)))) {
        ::GetWindowRect(window, &bounds);
    }
    return bounds;
}

} // namespace

bool RegionOverlay::IsShowing() { return g_isShowing; }

RegionOverlay::RegionOverlay() = default;

RegionOverlay::~RegionOverlay() {
    if (hwnd_) ::DestroyWindow(hwnd_);
    g_isShowing = false;
}

// ---------------------------------------------------------------------------

RegionOverlay::Selection RegionOverlay::Run(Style style) {
    style_ = style;

    // Freeze the desktop before the window goes up. Everything the overlay
    // draws is a composite of this image, so the overlay can never appear in
    // its own output and the screen cannot change mid-selection.
    frozen_ = capture::GrabVirtualDesktop(&desktopBounds_);
    if (!frozen_) {
        logging::Write(L"overlay: couldn't capture the desktop");
        return Selection{};
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW description{};
        description.cbSize        = sizeof(description);
        description.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        description.lpfnWndProc   = &RegionOverlay::WindowProc;
        description.hInstance     = ::GetModuleHandleW(nullptr);
        description.hCursor       = ::LoadCursorW(nullptr, IDC_CROSS);
        description.hbrBackground = nullptr;   // we paint every pixel ourselves
        description.lpszClassName = kWindowClass;
        if (!::RegisterClassExW(&description)) {
            logging::Write(L"overlay: couldn't register the window class");
            return Selection{};
        }
        registered = true;
    }

    if (style_ == Style::Adjustable) {
        // Not empty to begin with, so the first thing the user sees is
        // something they can drag: a 20% inset of the primary monitor.
        const RECT monitor = util::MonitorBounds(util::MonitorUnderCursor());
        const int width  = util::RectWidth(monitor);
        const int height = util::RectHeight(monitor);
        selection_.left   = monitor.left + static_cast<int>(width * 0.2);
        selection_.top    = monitor.top  + static_cast<int>(height * 0.2);
        selection_.right  = selection_.left + static_cast<int>(width * 0.6);
        selection_.bottom = selection_.top  + static_cast<int>(height * 0.6);
        // MonitorBounds is in virtual-desktop coordinates; everything else
        // here — drawing, hit-testing, the confirmed result — works in
        // frozen-bitmap coordinates. On a layout whose desktop origin is not
        // (0,0) the seed rectangle would otherwise be drawn in the wrong
        // place and record a doubly-offset region.
        ::OffsetRect(&selection_, -desktopBounds_.left, -desktopBounds_.top);
        hasSelection_ = true;
    }

    g_isShowing = true;

    hwnd_ = ::CreateWindowExW(
        // TOOLWINDOW keeps the overlay out of the taskbar and out of Alt-Tab.
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClass, L"SnipText", WS_POPUP,
        desktopBounds_.left, desktopBounds_.top,
        util::RectWidth(desktopBounds_), util::RectHeight(desktopBounds_),
        nullptr, nullptr, ::GetModuleHandleW(nullptr), this);

    if (!hwnd_) {
        g_isShowing = false;
        logging::Write(L"overlay: couldn't create the window");
        return Selection{};
    }

    ::ShowWindow(hwnd_, SW_SHOW);
    // Order matters: bring the window forward first, then take focus, or it
    // can end up visible but not receiving keystrokes — which would leave
    // Esc and Return undelivered.
    ::SetForegroundWindow(hwnd_);
    ::SetFocus(hwnd_);
    ::SetCursor(::LoadCursorW(nullptr, IDC_CROSS));

    // A modal loop of our own. The alternative — returning to the app's loop
    // and finishing in a callback — means every caller has to cope with a
    // selection that is half made, which is where the macOS version's
    // trickiest bugs lived.
    MSG message{};
    while (!finished_) {
        const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
        if (result == 0) {
            // WM_QUIT arrived while the overlay was up. Consuming it here
            // would lose the quit entirely — the app's own loop would never
            // see it — so put it back and treat the selection as cancelled.
            ::PostQuitMessage(static_cast<int>(message.wParam));
            result_.confirmed = false;
            break;
        }
        if (result == -1) break;
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    // g_isShowing stays true until this object is destroyed. That deliberately
    // spans the gap between the window disappearing and the caller acting on
    // the result, so a second overlay cannot be raised into a recording that
    // is about to start.
    return result_;
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK RegionOverlay::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    RegionOverlay* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<RegionOverlay*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<RegionOverlay*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    return self->HandleMessage(hwnd, message, wParam, lParam);
}

LRESULT RegionOverlay::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;   // every pixel is painted in WM_PAINT

    case WM_LBUTTONDOWN: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        OnMouseDown(hwnd, point);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        OnMouseMove(hwnd, point);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        OnMouseUp(hwnd, point);
        return 0;
    }

    case WM_RBUTTONUP:
        Cancel(hwnd);
        return 0;

    case WM_CAPTURECHANGED:
        // Capture can be taken away — a system dialog, a shell hook — and
        // when it is, no WM_LBUTTONUP ever arrives. Without this the Record
        // button stays latched dark and the drag state stays stale for the
        // rest of the overlay's life.
        //
        // The guard is essential: ReleaseCapture sends this message back
        // synchronously even when we are the ones releasing, so without it
        // OnMouseUp would find its own state already wiped and every click
        // would do nothing.
        if (releasingCapture_) return 0;
        pressedRecord_   = false;
        dragMode_        = DragMode::None;
        activeGrip_      = Grip::None;
        didStartNewRect_ = false;
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(hwnd, wParam);
        return 0;

    case WM_SETCURSOR:
        // Set explicitly rather than through the class cursor, so the grab
        // cursor tracks the live selection instead of staying pinned to
        // wherever it started.
        return TRUE;

    case WM_ACTIVATE:
        // Losing activation with an overlay up would leave a full-screen
        // window the user cannot dismiss. The suppression flag is for the one
        // case where the overlay hides itself on purpose — window picking,
        // which has to get out of the way of WindowFromPoint.
        if (LOWORD(wParam) == WA_INACTIVE && !finished_ && !suppressDeactivate_) Cancel(hwnd);
        return 0;

    case WM_DESTROY:
        finished_ = true;
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

// --- painting --------------------------------------------------------------

void RegionOverlay::OnPaint(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(hwnd, &paint);
    if (!dc) return;

    const RECT dirty = paint.rcPaint;
    const int  width  = util::RectWidth(dirty);
    const int  height = util::RectHeight(dirty);

    if (width > 0 && height > 0 && frozen_ && frozen_->MemoryDC()) {
        // Paint through an off-screen buffer the size of the dirty rectangle.
        // This view covers every monitor, and repainting a multi-megapixel
        // composite straight to the screen on each mouse-move is exactly what
        // makes a selection feel sluggish.
        auto buffer = Bitmap::Create(width, height);
        if (buffer && buffer->MemoryDC()) {
            HDC target = buffer->MemoryDC();
            ::SetViewportOrgEx(target, -dirty.left, -dirty.top, nullptr);

            ::BitBlt(target, dirty.left, dirty.top, width, height,
                     frozen_->MemoryDC(), dirty.left, dirty.top, SRCCOPY);
            DrawChrome(target);

            ::SetViewportOrgEx(target, 0, 0, nullptr);
            ::BitBlt(dc, dirty.left, dirty.top, width, height, target, 0, 0, SRCCOPY);
        }
    }

    ::EndPaint(hwnd, &paint);
}

// The caller has already clipped to the dirty rectangle by sizing the
// off-screen buffer and shifting its viewport origin, so this draws the whole
// chrome and lets the buffer do the clipping.
void RegionOverlay::DrawChrome(HDC dc) const {
    const RECT client{ 0, 0, util::RectWidth(desktopBounds_), util::RectHeight(desktopBounds_) };

    // Dim everything outside the selection. Four rectangles rather than an
    // even-odd region: the four are already clipped to the dirty rect by the
    // caller's viewport, and a region fill is not.
    if (hasSelection_ && util::RectWidth(selection_) > 0 && util::RectHeight(selection_) > 0) {
        RECT s = selection_;
        FillAlpha(dc, util::MakeRect(client.left, client.top, client.right, s.top), RGB(0, 0, 0), kDimAlpha);
        FillAlpha(dc, util::MakeRect(client.left, s.bottom, client.right, client.bottom), RGB(0, 0, 0), kDimAlpha);
        FillAlpha(dc, util::MakeRect(client.left, s.top, s.left, s.bottom), RGB(0, 0, 0), kDimAlpha);
        FillAlpha(dc, util::MakeRect(s.right, s.top, client.right, s.bottom), RGB(0, 0, 0), kDimAlpha);
    } else {
        FillAlpha(dc, client, RGB(0, 0, 0), kDimAlpha);
    }

    if (!hasSelection_ || util::RectWidth(selection_) <= 0 || util::RectHeight(selection_) <= 0) {
        return;
    }

    StrokeRect(dc, selection_, RGB(255, 255, 255), kBorderWidth);

    if (style_ == Style::Adjustable) {
        for (int i = static_cast<int>(Grip::TopLeft); i <= static_cast<int>(Grip::Left); ++i) {
            FillSolid(dc, GripRect(static_cast<Grip>(i)), RGB(255, 255, 255));
        }
    }

    // --- the size readout ---
    // Reported in RECORDED pixels: the selection scaled by the quality
    // setting, because that is what ends up in the file. A 600x400 selection
    // at Medium quality reads 450 x 300.
    const double outputScale = (style_ == Style::Adjustable)
                             ? video::CurrentQualityScale()
                             : 1.0;
    const std::wstring readout = util::Format(
        L"%d × %d",
        static_cast<int>(util::RectWidth(selection_) * outputScale),
        static_cast<int>(util::RectHeight(selection_) * outputScale));

    ScopedFont readoutFont = MakeFont(14, FW_SEMIBOLD);
    if (readoutFont) {
        SelectGuard fontGuard(dc, readoutFont.get());
        SIZE extent{};
        ::GetTextExtentPoint32W(dc, readout.c_str(), static_cast<int>(readout.size()), &extent);

        RECT box{};
        box.left   = selection_.right - extent.cx - kReadoutPadding * 2 - 4;
        box.right  = box.left + extent.cx + kReadoutPadding * 2;
        box.bottom = selection_.top - kReadoutPadding;
        box.top    = box.bottom - extent.cy - kReadoutPadding;
        // If the readout would fall off the top of the desktop, tuck it just
        // inside the selection instead.
        if (box.top < client.top) {
            box.top    = selection_.top + kReadoutPadding;
            box.bottom = box.top + extent.cy + kReadoutPadding;
        }

        FillAlpha(dc, box, RGB(0, 0, 0), kReadoutAlpha);
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, RGB(255, 255, 255));
        ::TextOutW(dc, box.left + kReadoutPadding, box.top + kReadoutPadding / 2,
                   readout.c_str(), static_cast<int>(readout.size()));
    }

    // --- controls ---
    ScopedFont hintFont = MakeFont(14, FW_MEDIUM);
    if (!hintFont) return;
    SelectGuard fontGuard(dc, hintFont.get());
    ::SetBkMode(dc, TRANSPARENT);

    if (style_ == Style::Adjustable) {
        const RECT button = RecordButtonRect();
        // Darkens while held, so a press that has not been released yet is
        // visibly a press.
        FillSolid(dc, button, pressedRecord_ ? RGB(164, 12, 25) : RGB(232, 17, 35));
        ::SetTextColor(dc, RGB(255, 255, 255));
        RECT label = button;
        ::DrawTextW(dc, L"Record", -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const RECT hint = HintRect();
    FillAlpha(dc, hint, RGB(0, 0, 0), kHintAlpha);
    ::SetTextColor(dc, RGB(255, 255, 255));
    RECT hintText = hint;
    const wchar_t* message =
        (style_ == Style::Adjustable)
            ? L"Drag to adjust  ·  Enter to record  ·  Esc to cancel"
            : (windowPickMode_
                   ? L"Click a window to capture it  ·  Space to drag instead  ·  Esc to cancel"
                   : L"Drag to select  ·  Space to pick a window  ·  Esc to cancel");
    ::DrawTextW(dc, message, -1, &hintText,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

// --- geometry --------------------------------------------------------------

RECT RegionOverlay::GripRect(Grip grip) const {
    const int midX = (selection_.left + selection_.right) / 2;
    const int midY = (selection_.top + selection_.bottom) / 2;
    POINT anchor{};
    switch (grip) {
    case Grip::TopLeft:     anchor = { selection_.left,  selection_.top };    break;
    case Grip::Top:         anchor = { midX,             selection_.top };    break;
    case Grip::TopRight:    anchor = { selection_.right, selection_.top };    break;
    case Grip::Right:       anchor = { selection_.right, midY };              break;
    case Grip::BottomRight: anchor = { selection_.right, selection_.bottom }; break;
    case Grip::Bottom:      anchor = { midX,             selection_.bottom }; break;
    case Grip::BottomLeft:  anchor = { selection_.left,  selection_.bottom }; break;
    case Grip::Left:        anchor = { selection_.left,  midY };              break;
    default:                return RECT{};
    }
    const int half = kGripSize / 2;
    return util::MakeRect(anchor.x - half, anchor.y - half, anchor.x + half, anchor.y + half);
}

RegionOverlay::Grip RegionOverlay::GripAt(POINT point) const {
    if (style_ != Style::Adjustable || !hasSelection_) return Grip::None;
    for (int i = static_cast<int>(Grip::TopLeft); i <= static_cast<int>(Grip::Left); ++i) {
        Grip grip = static_cast<Grip>(i);
        if (util::RectContains(util::InflateRect(GripRect(grip), kGripHitInflate, kGripHitInflate),
                               point)) {
            return grip;
        }
    }
    return Grip::None;
}

RECT RegionOverlay::RecordButtonRect() const {
    const int midX = (selection_.left + selection_.right) / 2;
    RECT button{};
    button.left  = midX - kButtonWidth / 2;
    button.right = button.left + kButtonWidth;

    // Centred in the selection, unless the selection is too short to hold the
    // button comfortably, in which case it goes below.
    if (util::RectHeight(selection_) < kButtonHeight * 5 / 2) {
        button.top = selection_.bottom + 10;
    } else {
        button.top = (selection_.top + selection_.bottom) / 2 - kButtonHeight / 2;
    }
    button.bottom = button.top + kButtonHeight;

    // A short selection against the bottom edge would otherwise put the
    // button off-screen, where it cannot be clicked at all. This is the one
    // definition both the drawing and the hit-testing use, so they cannot
    // disagree about where it went.
    const int desktopHeight = util::RectHeight(desktopBounds_);
    if (button.bottom > desktopHeight - 4) {
        button.top    = selection_.top - kButtonHeight - 10;
        button.bottom = button.top + kButtonHeight;
    }
    if (button.top < 4) {
        button.top    = 4;
        button.bottom = button.top + kButtonHeight;
    }

    // Horizontally too, for a selection hard against a side edge.
    const int desktopWidth = util::RectWidth(desktopBounds_);
    if (button.left < 4) {
        button.left  = 4;
        button.right = button.left + kButtonWidth;
    }
    if (button.right > desktopWidth - 4) {
        button.right = desktopWidth - 4;
        button.left  = button.right - kButtonWidth;
    }
    return button;
}

RECT RegionOverlay::HintRect() const {
    const int desktopWidth  = util::RectWidth(desktopBounds_);
    const int desktopHeight = util::RectHeight(desktopBounds_);

    int midX = desktopWidth / 2;
    if (hasSelection_) {
        midX = (selection_.left + selection_.right) / 2;
    } else {
        // Centred on the monitor under the pointer, not on the virtual
        // desktop — on a two-monitor setup the desktop's centre is the seam
        // between them, which would split the hint down the middle.
        const RECT monitor = util::MonitorBounds(util::MonitorUnderCursor());
        midX = (monitor.left + monitor.right) / 2 - desktopBounds_.left;
    }

    // The Record button sits below the selection when the selection is too
    // short to hold it inside, and the hint has to clear it or it paints its
    // translucent bar straight across the button's label. Asked of the real
    // function rather than re-derived, because RecordButtonRect also flips
    // the button above the selection near the bottom edge — and a second
    // copy of that rule would drift out of step with the first.
    const bool buttonIsBelow = (style_ == Style::Adjustable) && hasSelection_ &&
                               (RecordButtonRect().top > selection_.bottom);
    const int belowGap = buttonIsBelow ? kButtonHeight + 20 : 12;

    RECT hint{};
    hint.left   = midX - kHintWidth / 2;
    hint.right  = hint.left + kHintWidth;
    hint.top    = hasSelection_ ? selection_.bottom + belowGap : 60;
    hint.bottom = hint.top + kHintHeight;

    // Three placements, tried in order: just below the selection, just above
    // it, then tucked inside its bottom edge. A selection that fills the
    // screen has no outside, and a hint clipped off the bottom of the display
    // is worse than one sitting over the shot.
    if (hasSelection_ && hint.bottom > desktopHeight - 8) {
        hint.bottom = selection_.top - 12;
        hint.top    = hint.bottom - kHintHeight;
    }
    if (hint.top < 8) {
        hint.bottom = (hasSelection_ ? selection_.bottom : desktopHeight) - 12;
        hint.top    = hint.bottom - kHintHeight;
    }
    // Last resort: clamp into the desktop.
    if (hint.bottom > desktopHeight - 4) {
        hint.bottom = desktopHeight - 4;
        hint.top    = hint.bottom - kHintHeight;
    }
    if (hint.top < 4) { hint.top = 4; hint.bottom = hint.top + kHintHeight; }

    // Keep it horizontally on screen too, for a selection hard against an edge.
    if (hint.left < 8) { hint.left = 8; hint.right = hint.left + kHintWidth; }
    if (hint.right > desktopWidth - 8) {
        hint.right = desktopWidth - 8;
        hint.left  = hint.right - kHintWidth;
    }
    return hint;
}

RECT RegionOverlay::ClampToDesktop(RECT rect) const {
    const int width  = util::RectWidth(desktopBounds_);
    const int height = util::RectHeight(desktopBounds_);

    int rectWidth  = (std::max)(kMinimumSize, util::RectWidth(rect));
    int rectHeight = (std::max)(kMinimumSize, util::RectHeight(rect));
    rectWidth  = (std::min)(rectWidth, width);
    rectHeight = (std::min)(rectHeight, height);

    int left = (std::min)((std::max)(0, static_cast<int>(rect.left)), width - rectWidth);
    int top  = (std::min)((std::max)(0, static_cast<int>(rect.top)),  height - rectHeight);
    return util::MakeRect(left, top, left + rectWidth, top + rectHeight);
}

void RegionOverlay::SetSelection(HWND hwnd, const RECT& selection) {
    // Invalidate the union of the old and new rectangles, grown enough to
    // cover the border, the handles and the readout above. Repainting the
    // whole desktop on every mouse-move is the difference between a selection
    // that feels immediate and one that drags.
    //
    // The padding covers the border, the grips and the readout, because all
    // three are anchored within a few pixels of the selection. It does NOT
    // cover the hint bar or the Record button, and assuming it did was a bug:
    //
    //   - The hint bar is kHintWidth (560) wide and centred on the selection.
    //     Any selection narrower than kHintWidth - 2 * kInvalidatePadding —
    //     380 px, which is most of them — leaves the bar sticking out past the
    //     dirty rectangle at both ends. Those ends are never repainted, so the
    //     bar smears a trail behind it as the selection moves.
    //   - Both the bar and the button clamp against the screen edges, and both
    //     flip to the other side of the selection when there is no room. A
    //     flip moves them an arbitrary distance in one step, which no fixed
    //     padding can predict.
    //
    // So ask the geometry where the chrome is, before and after, instead of
    // padding and hoping. Two extra calls per mouse-move against arithmetic
    // this cheap does not measurably cost anything, and unlike a larger
    // padding it is exact for every selection size and screen position.
    const RECT beforeSelection = selection_;
    const bool hadSelection    = hasSelection_;
    const RECT beforeHint      = HintRect();
    const bool hadButton       = (style_ == Style::Adjustable) && hadSelection;
    const RECT beforeButton    = hadButton ? RecordButtonRect() : RECT{};

    selection_    = selection;
    hasSelection_ = true;

    RECT dirty = util::InflateRect(util::UnionRect(beforeSelection, selection_),
                                   kInvalidatePadding, kInvalidatePadding);

    // util::UnionRect is a plain min/max — unlike ::UnionRect it does not
    // ignore an empty rectangle, so an unguarded RECT{} here would drag the
    // dirty region all the way out to the origin and repaint the desktop.
    dirty = util::UnionRect(dirty, beforeHint);
    dirty = util::UnionRect(dirty, HintRect());
    if (hadButton)                      dirty = util::UnionRect(dirty, beforeButton);
    if (style_ == Style::Adjustable)    dirty = util::UnionRect(dirty, RecordButtonRect());

    ::InvalidateRect(hwnd, &dirty, FALSE);
}

// --- input -----------------------------------------------------------------

void RegionOverlay::OnMouseDown(HWND hwnd, POINT point) {
    ::SetCapture(hwnd);
    dragOrigin_         = point;
    dragStartSelection_ = selection_;
    activeGrip_         = Grip::None;
    didStartNewRect_    = false;

    if (windowPickMode_) {
        Confirm(hwnd);
        return;
    }

    if (style_ == Style::Adjustable) {
        // The Record button first of all. It sits inside the selection, so
        // without this the click that was aimed at it is read as "start
        // dragging the selection" and the button can never be pressed.
        if (util::RectContains(RecordButtonRect(), point)) {
            pressedRecord_ = true;
            dragMode_      = DragMode::None;
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        // Handles next: they are small, they sit on the outline, and they
        // are what you meant if you managed to hit one.
        Grip grip = GripAt(point);
        if (grip != Grip::None) {
            activeGrip_ = grip;
            dragMode_   = DragMode::Resizing;
            return;
        }
        if (hasSelection_ && util::RectContains(selection_, point)) {
            dragMode_ = DragMode::Moving;
            return;
        }
        // Outside the selection. Note the selection is NOT reset here: doing
        // that on mouse-down meant one stray click replaced a carefully sized
        // region with a 16-pixel square. The new rectangle only starts once
        // the pointer has actually moved.
        dragMode_ = DragMode::Drawing;
        return;
    }

    dragMode_ = DragMode::Drawing;
    hasSelection_ = false;
    SetSelection(hwnd, util::MakeRect(point.x, point.y, point.x, point.y));
    hasSelection_ = false;
}

void RegionOverlay::OnMouseMove(HWND hwnd, POINT point) {
    if (windowPickMode_) {
        PickWindowUnderCursor(hwnd);
        return;
    }

    if (dragMode_ == DragMode::None) {
        const wchar_t* cursor = IDC_CROSS;
        if (style_ == Style::Adjustable && hasSelection_) {
            // The Record button is checked before the selection, exactly as
            // the click handler checks it — so what the pointer says matches
            // what a click would do.
            if (util::RectContains(RecordButtonRect(), point)) {
                cursor = IDC_HAND;
            } else if (GripAt(point) != Grip::None) {
                cursor = IDC_SIZEALL;
            } else if (util::RectContains(selection_, point)) {
                cursor = IDC_SIZEALL;
            }
        }
        ::SetCursor(::LoadCursorW(nullptr, cursor));
        return;
    }

    const int dx = point.x - dragOrigin_.x;
    const int dy = point.y - dragOrigin_.y;

    switch (dragMode_) {
    case DragMode::Drawing: {
        if (style_ == Style::Adjustable && std::hypot(dx, dy) <= kNewRectDeadZone) return;
        didStartNewRect_ = true;
        SetSelection(hwnd, util::NormalizedRect(dragOrigin_, point));
        break;
    }
    case DragMode::Moving: {
        RECT moved = dragStartSelection_;
        ::OffsetRect(&moved, dx, dy);
        // Slides against the desktop edges without changing size.
        const int width  = util::RectWidth(moved);
        const int height = util::RectHeight(moved);
        moved.left = (std::min)((std::max)(0, static_cast<int>(moved.left)),
                                util::RectWidth(desktopBounds_) - width);
        moved.top  = (std::min)((std::max)(0, static_cast<int>(moved.top)),
                                util::RectHeight(desktopBounds_) - height);
        moved.right  = moved.left + width;
        moved.bottom = moved.top + height;
        SetSelection(hwnd, moved);
        break;
    }
    case DragMode::Resizing: {
        // Always computed from the rectangle as it was when the drag began,
        // never from the live one. Recomputing from the live rect makes the
        // dragged edge chase the cursor once the shape flips through zero,
        // collapsing it to a sliver.
        RECT next = dragStartSelection_;
        switch (activeGrip_) {
        case Grip::TopLeft:     next.left += dx; next.top += dy;    break;
        case Grip::Top:         next.top += dy;                     break;
        case Grip::TopRight:    next.right += dx; next.top += dy;   break;
        case Grip::Right:       next.right += dx;                   break;
        case Grip::BottomRight: next.right += dx; next.bottom += dy;break;
        case Grip::Bottom:      next.bottom += dy;                  break;
        case Grip::BottomLeft:  next.left += dx; next.bottom += dy; break;
        case Grip::Left:        next.left += dx;                    break;
        default: break;
        }
        POINT topLeft{ next.left, next.top };
        POINT bottomRight{ next.right, next.bottom };
        SetSelection(hwnd, ClampToDesktop(util::NormalizedRect(topLeft, bottomRight)));
        break;
    }
    default:
        break;
    }
}

void RegionOverlay::OnMouseUp(HWND hwnd, POINT point) {
    releasingCapture_ = true;
    ::ReleaseCapture();
    releasingCapture_ = false;

    // A button press only counts if the release lands on it too, which is
    // what lets someone press it, think better of it, and slide off.
    if (pressedRecord_) {
        pressedRecord_ = false;
        ::InvalidateRect(hwnd, nullptr, FALSE);
        if (util::RectContains(RecordButtonRect(), point)) Confirm(hwnd);
        return;
    }

    if (dragMode_ == DragMode::Drawing && style_ == Style::Instant) {
        RECT drawn = util::NormalizedRect(dragOrigin_, point);
        // A click rather than a drag: cancel, don't hand back a 1-pixel snip.
        if (util::RectWidth(drawn) < kMinimumSize || util::RectHeight(drawn) < kMinimumSize) {
            dragMode_ = DragMode::None;
            Cancel(hwnd);
            return;
        }
        SetSelection(hwnd, drawn);
        dragMode_ = DragMode::None;
        Confirm(hwnd);
        return;
    }

    // Clamp only when a new rectangle was actually begun. A straight vertical
    // drag produces a zero-width rect that most needs the minimum applied,
    // while a click that never moved must leave the previous selection alone.
    if (dragMode_ == DragMode::Drawing && didStartNewRect_) {
        SetSelection(hwnd, ClampToDesktop(selection_));
    }

    dragMode_        = DragMode::None;
    activeGrip_      = Grip::None;
    didStartNewRect_ = false;
}

void RegionOverlay::OnKeyDown(HWND hwnd, WPARAM key) {
    switch (key) {
    case VK_ESCAPE:
        Cancel(hwnd);
        return;
    case VK_RETURN:
        if (style_ == Style::Adjustable) Confirm(hwnd);
        return;
    case VK_SPACE:
        if (style_ == Style::Instant && dragMode_ == DragMode::None) {
            windowPickMode_ = !windowPickMode_;
            if (windowPickMode_) {
                PickWindowUnderCursor(hwnd);
            } else {
                hasSelection_ = false;
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return;
    default:
        return;
    }
}

void RegionOverlay::PickWindowUnderCursor(HWND hwnd) {
    POINT cursor{};
    ::GetCursorPos(&cursor);

    // The overlay is on top of everything, so ask what is beneath it. Hiding
    // the foreground window sends WM_ACTIVATE/WA_INACTIVE, which the handler
    // above would otherwise read as "the user clicked away" and cancel.
    suppressDeactivate_ = true;
    ::ShowWindow(hwnd, SW_HIDE);
    HWND under = ::WindowFromPoint(cursor);
    ::ShowWindow(hwnd, SW_SHOWNA);
    ::SetForegroundWindow(hwnd);
    suppressDeactivate_ = false;

    if (!under) return;
    // Walk up to the top-level window: picking a button inside a dialog is
    // almost never what someone means by "that window".
    HWND root = ::GetAncestor(under, GA_ROOT);
    if (root) under = root;
    if (under == hwnd) return;

    RECT bounds = VisibleWindowBounds(under);
    ::OffsetRect(&bounds, -desktopBounds_.left, -desktopBounds_.top);
    if (util::RectWidth(bounds) < kMinimumSize || util::RectHeight(bounds) < kMinimumSize) return;

    SetSelection(hwnd, ClampToDesktop(bounds));
}

// --- finishing -------------------------------------------------------------

void RegionOverlay::Confirm(HWND hwnd) {
    if (!hasSelection_ ||
        util::RectWidth(selection_) < kMinimumSize ||
        util::RectHeight(selection_) < kMinimumSize) {
        // Too small to be meaningful. The overlay deliberately stays up
        // rather than cancelling out from under the user.
        ::MessageBeep(MB_ICONWARNING);
        return;
    }

    result_.confirmed = true;
    result_.bounds    = selection_;   // client coordinates == frozen-bitmap pixels
    finished_         = true;

    // Hide immediately. Everything downstream — a recording starting, an
    // editor opening — must happen with the overlay already gone. A posted
    // WM_NULL wakes the modal loop so it can notice `finished_`; posting
    // WM_QUIT instead would leave a quit message in the thread queue that the
    // app's own loop might later pick up.
    ::ShowWindow(hwnd, SW_HIDE);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
}

void RegionOverlay::Cancel(HWND hwnd) {
    result_.confirmed = false;
    finished_         = true;
    ::ShowWindow(hwnd, SW_HIDE);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
}
