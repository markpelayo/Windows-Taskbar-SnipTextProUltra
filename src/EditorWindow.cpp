#include "EditorWindow.h"

#include "EditorSettings.h"
#include "MediaFolder.h"
#include "Settings.h"
#include "Util.h"

#include <commctrl.h>
#include <dwmapi.h>

// See the note in Bitmap.cpp: gdiplustypes.h needs min/max, and this project
// builds with NOMINMAX.
#include <algorithm>
#include <cmath>
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
constexpr const wchar_t* kFrameClass  = L"SnipTextProUltraEditorFrame";
constexpr const wchar_t* kCanvasClass = L"SnipTextProUltraEditorCanvas";
constexpr const wchar_t* kPopupClass  = L"SnipTextProUltraColourPopup";

constexpr int IDC_UNDO       = 101;
constexpr int IDC_REDO       = 102;
constexpr int IDC_COPY       = 103;
constexpr int IDC_SAVE       = 104;
constexpr int IDC_SWATCH     = 105;
constexpr int IDC_SLIDER     = 106;
constexpr int IDC_PINTOGGLE  = 107;
constexpr int IDC_TOOL_FIRST = 110;
// 200, not 120. The tool buttons run from IDC_TOOL_FIRST upwards, one per
// tool, and at eight tools they reach 117 — three short of where this used to
// sit. A tenth tool would have collided with it, and the failure would have
// been quiet: a tool button whose click is read as an edit-control
// notification.
constexpr int IDC_TEXTEDIT   = 200;

constexpr UINT_PTR kTitleFlashTimer = 1;
constexpr UINT     kTitleFlashMs    = 1200;

// Posted to the frame so the system colour picker opens on a later
// message-loop turn, after the popup that requested it has finished
// destroying itself.
constexpr UINT WM_OPEN_COLOUR_PICKER = WM_APP + 1;

constexpr int kBarHeight     = 44;
constexpr int kBarPadding    = 10;
constexpr int kButtonHeight  = 28;
// Tools are square icons now, not words. Eight text labels would have needed
// a minimum window wider than the old seven did; eight icons need markedly
// less room than the seven words they replaced. Every glyph is drawn in GDI
// from lines and curves — there is no image resource anywhere in the program
// and adding one for this would have been the first.
constexpr int kToolWidth     = 34;
constexpr int kToolGap       = 4;
constexpr int kSwatchWidth   = 44;
constexpr int kSliderWidth   = 120;


// The narrowest the window may be without clipping a toolbar button off the
// right edge. Derived, because a literal here is a number that has to be
// remembered every time a button is added — and is not, which is how the Lift
// button went missing on small captures: the minimum size was raised for
// resizing but the CREATION size still had its own copy of the old literal,
// so any capture small enough to hit the floor opened one button short.
//
// Two rows have to fit now, so the floor is whichever needs more.
//
// Bottom: padding, swatch, slider, then the tool icons.
constexpr int kMinToolRowWidth = kBarPadding
                               + kSwatchWidth + 6
                               + kSliderWidth + 12
                               + kToolCount * kToolWidth + (kToolCount - 1) * kToolGap
                               + kBarPadding;

// Top: three groups. Undo and Redo anchored left, Pin centred, Copy and
// Save anchored right. Two icon buttons each side, so both flanks are the
// same width by construction and the centre really is the centre.
//
// The floor is where the centred control would touch a flank. Because it is
// centred, the wider flank has to be reserved on BOTH sides — plus a gutter,
// without which the three meet exactly at the minimum width and it reads as
// a rendering fault rather than a deliberate limit.
constexpr int kCommandGutter   = 12;
constexpr int kFlankWidth      = kBarPadding + kToolWidth * 2 + kToolGap;
constexpr int kMinCommandRowWidth = kToolWidth + (kFlankWidth + kCommandGutter) * 2;

constexpr int kMinContentWidth = (kMinToolRowWidth > kMinCommandRowWidth)
                                     ? kMinToolRowWidth : kMinCommandRowWidth;

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

// --- tool glyphs -----------------------------------------------------------
//
// Each is drawn inside a notional 20 x 20 box and mapped into whatever the
// button turns out to be, so the same code produces the toolbar icon and the
// 3x version in the documentation. Lines rather than a font: a glyph font
// would have to be either shipped (a resource, and this program has none) or
// borrowed from the system (and Segoe MDL2 is not on every supported build).
//
// ExtCreatePen rather than CreatePen because round caps and joins are the
// difference between a drawn arrow and a bundle of sticks, and the plain
// CreatePen has no way to ask for them.

constexpr double kPi = 3.14159265358979323846;

struct Glyph {
    HDC    dc;
    double left;
    double top;
    double unit;    // device pixels per logical unit

    POINT At(double x, double y) const {
        return POINT{ static_cast<LONG>(std::lround(left + x * unit)),
                      static_cast<LONG>(std::lround(top  + y * unit)) };
    }
    void Line(double x0, double y0, double x1, double y1) const {
        const POINT a = At(x0, y0);
        const POINT b = At(x1, y1);
        ::MoveToEx(dc, a.x, a.y, nullptr);
        ::LineTo(dc, b.x, b.y);
    }
    void Triangle(double x0, double y0, double x1, double y1,
                  double x2, double y2, HBRUSH fill) const {
        const POINT points[3] = { At(x0, y0), At(x1, y1), At(x2, y2) };
        SelectGuard brushGuard(dc, fill);
        SelectGuard penGuard(dc, ::GetStockObject(NULL_PEN));
        ::Polygon(dc, points, 3);
    }
    void Box(double x0, double y0, double x1, double y1, HBRUSH fill) const {
        const POINT a = At(x0, y0);
        const POINT b = At(x1, y1);
        RECT rect{ a.x, a.y, b.x, b.y };
        ::FillRect(dc, &rect, fill);
    }
};

ScopedPen MakeGlyphPen(COLORREF ink, double widthPixels) {
    LOGBRUSH brush{};
    brush.lbStyle = BS_SOLID;
    brush.lbColor = ink;
    return ScopedPen(::ExtCreatePen(
        PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        static_cast<DWORD>((std::max)(1L, std::lround(widthPixels))), &brush, 0, nullptr));
}

// The four commands and the Pin toggle. A separate enum from Tool because
// they are a different kind of thing — a tool changes what the next drag
// does and stays selected; these happen once and are over — but they are
// drawn by the same code at the same size, which is the whole point of
// making them icons.
enum class Command { Undo, Redo, Copy, Save, Pin };

void DrawCommandGlyph(HDC dc, Command command, const RECT& box, COLORREF ink) {
    const int side = (std::min)(util::RectWidth(box), util::RectHeight(box));
    if (side <= 0) return;

    Glyph g{};
    g.dc   = dc;
    g.unit = side / 20.0;
    g.left = box.left + (util::RectWidth(box)  - side) / 2.0;
    g.top  = box.top  + (util::RectHeight(box) - side) / 2.0;

    ScopedPen pen = MakeGlyphPen(ink, g.unit * 1.7);
    ScopedBrush fill(::CreateSolidBrush(ink));
    if (!pen || !fill) return;

    SelectGuard penGuard(dc, pen.get());
    SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));

    switch (command) {
    case Command::Undo:
    case Command::Redo: {
        // A straight shaft that turns into a hook: the arrow doubles back
        // on itself, which is the whole idea of undo drawn in one stroke.
        //
        // Mirrored rather than drawn twice, so the pair can never drift
        // apart — every coordinate goes through mx().
        const bool forward = (command == Command::Redo);
        auto mx = [forward](double x) { return forward ? 20.0 - x : x; };

        constexpr double cx = 11.0, cy = 12.5, r = 4.5;

        // Generated rather than hand-fitted with Béziers. GDI's Arc() takes
        // its direction from the coordinate system, which points down here,
        // so "counterclockwise" draws clockwise on screen and the pair of
        // them would have to be reasoned about separately. A short
        // polyline sidesteps that entirely, and with the round joins from
        // ExtCreatePen nobody can see the segments at twenty pixels.
        constexpr int kSteps = 24;
        POINT hook[kSteps + 1];
        for (int i = 0; i <= kSteps; ++i) {
            // 90 degrees is the top of the circle, sweeping 240 degrees
            // clockwise on screen: top, round the outside, back past the
            // bottom. Stopping at a half-circle looks like a bracket; the
            // extra 60 degrees is what makes it read as a return.
            const double degrees = 90.0 - 240.0 * i / kSteps;
            const double radians = degrees * kPi / 180.0;
            hook[i] = g.At(mx(cx + r * std::cos(radians)), cy - r * std::sin(radians));
        }
        ::Polyline(dc, hook, kSteps + 1);

        // The shaft runs left out of the top of the hook, and ends in an
        // open chevron rather than a filled triangle — at this size a solid
        // head closes up into a blob.
        g.Line(mx(cx), cy - r, mx(3.5), cy - r);
        g.Line(mx(3.5), cy - r, mx(7.3), cy - r - 3.8);
        g.Line(mx(3.5), cy - r, mx(7.3), cy - r + 3.8);
        break;
    }

    case Command::Copy: {
        // Two sheets, one behind the other. The back sheet is drawn as only
        // the part of it you would actually see — an L around the top and
        // right — rather than a full rectangle hidden by a filled front
        // sheet. Filling would have meant knowing the button's face colour,
        // which changes when it is pressed, and a glyph that has to be told
        // what it is sitting on is a glyph that will be wrong somewhere.
        const POINT behind[5] = {
            g.At(6.5, 6.5), g.At(6.5, 3.0), g.At(17.0, 3.0),
            g.At(17.0, 13.5), g.At(13.5, 13.5)
        };
        ::Polyline(dc, behind, 5);

        const POINT c = g.At(3.0, 6.5);
        const POINT d = g.At(13.5, 17.0);
        ::Rectangle(dc, c.x, c.y, d.x, d.y);
        break;
    }

    case Command::Save: {
        // A floppy disk. Nothing else is read as "save" without a caption,
        // and every attempt at something more modern — a downward arrow, a
        // tray — reads as "download" instead.
        const POINT a = g.At(3.0, 3.5);
        const POINT b = g.At(17.0, 16.5);
        ::Rectangle(dc, a.x, a.y, b.x, b.y);
        g.Box(7.0, 3.5, 13.0, 8.0, fill.get());      // the shutter
        const POINT c = g.At(6.0, 10.5);
        const POINT d = g.At(14.0, 16.5);
        ::Rectangle(dc, c.x, c.y, d.x, d.y);          // the label
        break;
    }

    case Command::Pin: {
        // A pushpin seen head-on: round head, crossbar, needle. Drawn
        // rather than borrowed, like everything else here.
        const POINT head[2] = { g.At(6.6, 3.0), g.At(13.4, 9.8) };
        SelectGuard headBrush(dc, fill.get());
        ::Ellipse(dc, head[0].x, head[0].y, head[1].x, head[1].y);
        g.Line(4.6, 11.0, 15.4, 11.0);
        g.Line(10.0, 11.5, 10.0, 17.5);
        break;
    }
    }
}

// One face for every icon button on either bar. The top row and the bottom
// row are the same kind of control at the same size, so they are drawn by
// the same function rather than by three near-copies that drift apart the
// first time one of them is adjusted.
//
// `active` is "this is switched on" — the selected tool, or Pin when it is
// enabled. It is the only state that survives letting go of the mouse, so
// it gets a doubled ring: at 34px one pixel of blue is easy to miss across
// a desk, and two is not.
void DrawIconButtonFace(HDC dc, const RECT& box, bool active, bool pressed) {
    ScopedBrush face(::CreateSolidBrush(
        (active || pressed) ? RGB(204, 228, 246) : RGB(253, 253, 253)));
    if (face) ::FillRect(dc, &box, face.get());

    ScopedBrush edge(::CreateSolidBrush(active ? RGB(0, 103, 192) : RGB(195, 199, 204)));
    if (!edge) return;
    ::FrameRect(dc, &box, edge.get());
    if (active) {
        RECT inner = box;
        ::InflateRect(&inner, -1, -1);
        ::FrameRect(dc, &inner, edge.get());
    }
}

void DrawToolGlyph(HDC dc, Tool tool, const RECT& box, COLORREF ink) {
    const int side = (std::min)(util::RectWidth(box), util::RectHeight(box));
    if (side <= 0) return;

    Glyph g{};
    g.dc   = dc;
    g.unit = side / 20.0;
    g.left = box.left + (util::RectWidth(box)  - side) / 2.0;
    g.top  = box.top  + (util::RectHeight(box) - side) / 2.0;

    ScopedPen pen = MakeGlyphPen(ink, g.unit * 1.7);
    ScopedBrush fill(::CreateSolidBrush(ink));
    if (!pen || !fill) return;

    SelectGuard penGuard(dc, pen.get());
    SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));
    const int previousBk = ::SetBkMode(dc, TRANSPARENT);

    switch (tool) {
    case Tool::Arrow:
        g.Line(3.5, 16.5, 15.0, 5.5);
        g.Triangle(16.5, 4.0, 9.4, 5.6, 14.9, 11.2, fill.get());
        break;

    case Tool::Rectangle: {
        const POINT a = g.At(3, 5.5);
        const POINT b = g.At(17, 15.5);
        ::Rectangle(dc, a.x, a.y, b.x, b.y);
        break;
    }

    case Tool::Ellipse: {
        const POINT a = g.At(2.8, 5.3);
        const POINT b = g.At(17.2, 15.7);
        ::Ellipse(dc, a.x, a.y, b.x, b.y);
        break;
    }

    case Tool::Line:
        g.Line(3.5, 16.5, 16.5, 4.5);
        break;

    case Tool::Pen: {
        // A stroke someone actually drew, rather than a picture of a pen: at
        // twenty pixels a nib turns to mush, and the squiggle says freehand
        // without needing to be recognised as an object.
        const POINT curve[4] = { g.At(3, 15.5), g.At(6.5, 6.0),
                                 g.At(11.5, 17.5), g.At(17, 6.5) };
        ::PolyBezier(dc, curve, 4);
        break;
    }

    case Tool::Text:
        g.Line(4.2, 16.5, 10.0, 4.5);
        g.Line(10.0, 4.5, 15.8, 16.5);
        g.Line(6.9, 12.0, 13.1, 12.0);
        break;

    case Tool::Lift: {
        // A marquee with solid corners: "take this piece out", which is what
        // distinguishes it from the plain Rectangle above.
        ScopedPen dashed(::CreatePen(PS_DOT, 1, ink));
        if (dashed) {
            SelectGuard dashGuard(dc, dashed.get());
            const POINT a = g.At(4, 6);
            const POINT b = g.At(16, 14.5);
            ::Rectangle(dc, a.x, a.y, b.x, b.y);
        }
        g.Box(2.2, 4.2, 5.0, 7.0, fill.get());
        g.Box(15.0, 4.2, 17.8, 7.0, fill.get());
        g.Box(2.2, 13.4, 5.0, 16.2, fill.get());
        g.Box(15.0, 13.4, 17.8, 16.2, fill.get());
        break;
    }

    case Tool::Callout:
        // Reversed from the first draft, to say what the tool now does: the
        // LETTER comes first and the arrow leaves it, pointing away at
        // something off the edge of the icon. Arrow-then-letter read as
        // "the text is the destination", which is exactly the placement
        // this release moved away from.
        g.Line(2.0, 17.5, 6.2, 6.5);
        g.Line(6.2, 6.5, 10.4, 17.5);
        g.Line(3.8, 13.5, 8.6, 13.5);
        g.Line(12.2, 14.0, 17.4, 8.2);
        g.Triangle(18.6, 6.8, 13.9, 7.6, 17.8, 11.5, fill.get());
        break;
    }

    ::SetBkMode(dc, previousBk);
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
    LiveEditors().push_back(this);
}

EditorWindow::~EditorWindow() {
    // The object outlives its window by one message-loop turn, because App
    // defers the delete — so between WM_DESTROY and the reap this editor is
    // still listed. That is why WM_DESTROY nulls pinButton_: a
    // PinSettingChanged arriving in the gap would otherwise call
    // InvalidateRect on a destroyed child. (It would return FALSE rather
    // than crash, but a guard that works by accident is not a guard.)
    LiveEditors().erase(
        std::remove(LiveEditors().begin(), LiveEditors().end(), this),
        LiveEditors().end());
}

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
        return false;
    }

    ::ShowWindow(hwnd_, SW_SHOW);
    ::SetForegroundWindow(hwnd_);
    ApplyAlwaysOnTop();
    ReturnFocusToCanvas();
    return true;
}

void EditorWindow::ApplyAlwaysOnTop() {
    if (!hwnd_) return;
    // The whole of "Pin to Screen" is this one call. The first version of
    // the feature floated a separate borderless copy of the picture, which
    // is not what pinning is for: the window worth keeping in front of you
    // is the one with the tools in it, not a read-only duplicate.
    //
    // SWP_NOACTIVATE so that toggling it in one editor does not snatch
    // focus into a different one.
    const bool onTop = settings::GetBool(settings::key::kPinToScreen, false);
    ::SetWindowPos(hwnd_, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // A topmost window is, by definition, always in the way — so while it
    // is on top it is also taken out of every capture the program makes.
    // Without this, turning the setting on would mean the editor appeared
    // in the next region shot, the next full-screen shot and every
    // recording, and the one thing you cannot do is move it aside, because
    // you pinned it there on purpose.
    //
    // Undone when the setting goes off: an ordinary window has no business
    // being invisible to the recorder, and you may well want to capture
    // the editor itself.
    if (onTop) util::ExcludeFromCapture(hwnd_);
    else       util::IncludeInCapture(hwnd_);
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
        // Icons, like the tools, because a bar that is half words and half
        // pictures reads as two bars that happen to be touching. Save keeps
        // a visual emphasis of its own — an accent ring drawn in
        // WM_DRAWITEM — since BS_DEFPUSHBUTTON's ring goes away with
        // owner-drawing and Save is still the primary action here.
        undoButton_ = MakeButton(hwnd_, L"", IDC_UNDO, BS_OWNERDRAW);
        redoButton_ = MakeButton(hwnd_, L"", IDC_REDO, BS_OWNERDRAW);
        copyButton_ = MakeButton(hwnd_, L"", IDC_COPY, BS_OWNERDRAW);
        saveButton_ = MakeButton(hwnd_, L"", IDC_SAVE, BS_OWNERDRAW);

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

        // A real toggle, writing the same registry value the tray row does,
        // so the two are one setting with two switches.
        //
        // Icon-only like everything else on the bar, which puts the whole
        // weight of "is this on?" on the button's appearance: switched on,
        // it takes the filled face and doubled accent ring that a selected
        // tool takes, so the two bars use one visual language for one idea.
        // The tooltip spells the state out in words for anyone who wants
        // it confirmed, and is rewritten on every toggle.
        pinButton_ = MakeButton(hwnd_, L"", IDC_PINTOGGLE, BS_OWNERDRAW);

        // BS_OWNERDRAW, so the check state has to be tracked rather than
        // asked for — which it already is, in currentTool_. The old
        // BS_AUTOCHECKBOX|BS_PUSHLIKE pair kept a second copy of that state
        // in the control, and two copies of one fact is one too many.
        for (int i = 0; i < kToolCount; ++i) {
            toolButtons_[i] = MakeButton(hwnd_, L"", IDC_TOOL_FIRST + i, BS_OWNERDRAW);
        }

        // Unlabelled squares without tooltips would be a guessing game.
        // TTF_SUBCLASS so the tooltip control hooks each button itself; the
        // alternative is relaying every mouse message by hand.
        tooltips_ = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                      WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      hwnd_, nullptr,
                                      ::GetModuleHandleW(nullptr), nullptr);
        if (tooltips_) {
            auto addTip = [&](HWND control, const wchar_t* text) {
                if (!control) return;
                TOOLINFOW info{};
                info.cbSize   = sizeof(info);
                info.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
                info.hwnd     = hwnd_;
                info.uId      = reinterpret_cast<UINT_PTR>(control);
                // const_cast: TOOLINFO's lpszText is a non-const pointer
                // even for a string the control only ever reads.
                info.lpszText = const_cast<LPWSTR>(text);
                ::SendMessageW(tooltips_, TTM_ADDTOOLW, 0,
                               reinterpret_cast<LPARAM>(&info));
            };
            for (int i = 0; i < kToolCount; ++i) {
                addTip(toolButtons_[i], ToolTitle(static_cast<Tool>(i)));
            }
            addTip(undoButton_, L"Undo  (Ctrl+Z)");
            addTip(redoButton_, L"Redo  (Ctrl+Y)");
            addTip(copyButton_, L"Copy to clipboard  (Ctrl+C)");
            addTip(saveButton_, L"Save as PNG\u2026  (Ctrl+S)");
            // Placeholder only; UpdatePinTooltip immediately replaces it
            // with the wording for the current state. Added here so the
            // tool exists for TTM_UPDATETIPTEXT to find later.
            addTip(pinButton_, L"Pin to Screen");
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
        // hwnd_, not hwnd: OnFrameMessage is a member and takes no window
        // handle — FrameProc has already resolved it and stored it.
        //
        // The fallback is not decoration. GetDpiForWindow returns 0 for a
        // handle it does not like, and AdjustWindowRectExForDpi given 0 does
        // not fail loudly — it produces a minimum size that is quietly wrong,
        // which is the same class of bug this whole change is fixing.
        const UINT dpi = hwnd_ ? ::GetDpiForWindow(hwnd_) : ::GetDpiForSystem();

        RECT frame{ 0, 0, kMinContentWidth, kMinContentHeight };
        ::AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                   dpi ? dpi : USER_DEFAULT_SCREEN_DPI);
        info->ptMinTrackSize.x = util::RectWidth(frame);
        info->ptMinTrackSize.y = util::RectHeight(frame);
        return 0;
    }

    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

        if (item->CtlID == IDC_SWATCH) {
            RECT box = item->rcItem;
            ::InflateRect(&box, -2, -3);
            ScopedBrush fill(::CreateSolidBrush(ActiveColour()));
            if (fill) ::FillRect(item->hDC, &box, fill.get());
            // A white swatch needs the outline to be visible at all against
            // the toolbar behind it.
            ScopedPen border(::CreatePen(PS_SOLID, 1, RGB(128, 128, 128)));
            if (border) {
                SelectGuard penGuard(item->hDC, border.get());
                SelectGuard brushGuard(item->hDC, ::GetStockObject(NULL_BRUSH));
                ::Rectangle(item->hDC, box.left, box.top, box.right, box.bottom);
            }
            return TRUE;
        }

        // --- every icon button, top bar and bottom, drawn the same way ---
        const bool isTool = item->CtlID >= static_cast<UINT>(IDC_TOOL_FIRST) &&
                            item->CtlID <  static_cast<UINT>(IDC_TOOL_FIRST + kToolCount);
        const bool isCommand = item->CtlID == IDC_UNDO || item->CtlID == IDC_REDO ||
                               item->CtlID == IDC_COPY || item->CtlID == IDC_SAVE ||
                               item->CtlID == IDC_PINTOGGLE;
        if (isTool || isCommand) {
            const bool disabled = (item->itemState & ODS_DISABLED) != 0;
            const bool pressed  = (item->itemState & ODS_SELECTED) != 0;

            // "Switched on", which only two kinds of button can be: the
            // selected tool, and Pin when it is enabled. Undo, Redo, Copy
            // and Save happen and are over.
            bool active = false;
            if (isTool) {
                active = (static_cast<Tool>(item->CtlID - IDC_TOOL_FIRST) == currentTool_);
            } else if (item->CtlID == IDC_PINTOGGLE) {
                active = settings::GetBool(settings::key::kPinToScreen, false);
            }

            RECT box = item->rcItem;
            DrawIconButtonFace(item->hDC, box, active, pressed);

            RECT glyphBox = box;
            ::InflateRect(&glyphBox, -5, -4);
            const COLORREF ink = disabled ? RGB(167, 173, 179)
                               : active   ? RGB(0, 90, 168)
                                          : RGB(35, 41, 47);
            if (isTool) {
                DrawToolGlyph(item->hDC, static_cast<Tool>(item->CtlID - IDC_TOOL_FIRST),
                              glyphBox, ink);
            } else {
                Command command = Command::Undo;
                switch (item->CtlID) {
                case IDC_REDO:      command = Command::Redo; break;
                case IDC_COPY:      command = Command::Copy; break;
                case IDC_SAVE:      command = Command::Save; break;
                case IDC_PINTOGGLE: command = Command::Pin;  break;
                default:            command = Command::Undo; break;
                }
                DrawCommandGlyph(item->hDC, command, glyphBox, ink);
            }

            if ((item->itemState & ODS_FOCUS) != 0) {
                RECT focus = box;
                ::InflateRect(&focus, -3, -3);
                ::DrawFocusRect(item->hDC, &focus);
            }
            return TRUE;
        }
        break;
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
        case IDC_PINTOGGLE: {
            // Writes the same value the tray row writes, and removes rather
            // than stores false — so "never touched" and "switched off
            // again" stay the same state, which is what lets Sanitize tell
            // whether there is anything to restore.
            const bool on = !settings::GetBool(settings::key::kPinToScreen, false);
            if (on) settings::SetBool(settings::key::kPinToScreen, true);
            else    settings::Remove(settings::key::kPinToScreen);
            // Every open editor, not just this one. Two windows showing
            // one setting must not disagree about it.
            PinSettingChanged();
            ReturnFocusToCanvas();
            return 0;
        }
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
        // Children are destroyed after the parent's WM_DESTROY, so these
        // handles are about to become invalid while this object is still
        // listed in LiveEditors — see the note in the destructor.
        pinButton_ = nullptr;
        tooltips_  = nullptr;
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
    auto place = [&](HWND control, int w, int h, int y, int gap = 6) {
        if (control) ::MoveWindow(control, x, y, w, h, TRUE);
        x += w + gap;
    };

    // --- top bar: history left, the switch centred, output right ---
    //
    // Three groups, each anchored to its own edge, so they are positioned
    // independently and cannot be pushed into one another by the window
    // getting wider. What CAN collide is the centred control meeting a
    // flank, and that is what kMinCommandRowWidth is for — WM_GETMINMAXINFO
    // stops the window before it happens, so no clamping is needed here.
    //
    // Reading left to right: what you did, what will happen next, where it
    // goes. Undo beside nothing else it could be confused with, and Save at
    // the far end where a final action belongs.
    x = kBarPadding;
    place(undoButton_, kToolWidth, kButtonHeight, topY, kToolGap);
    place(redoButton_, kToolWidth, kButtonHeight, topY, kToolGap);

    if (pinButton_) {
        ::MoveWindow(pinButton_, (width - kToolWidth) / 2, topY,
                     kToolWidth, kButtonHeight, TRUE);
    }

    // Laid out from the right edge inwards, so Save is always the last
    // thing on the row whatever the window is doing.
    int rightX = width - kBarPadding - kToolWidth;
    if (saveButton_) {
        ::MoveWindow(saveButton_, rightX, topY, kToolWidth, kButtonHeight, TRUE);
    }
    rightX -= kToolWidth + kToolGap;
    if (copyButton_) {
        ::MoveWindow(copyButton_, rightX, topY, kToolWidth, kButtonHeight, TRUE);
    }

    // --- bottom bar: style, then tools ---
    const int bottomY = height - kBarHeight + (kBarHeight - kButtonHeight) / 2;
    x = kBarPadding;
    place(swatch_, kSwatchWidth, kButtonHeight, bottomY);
    place(slider_, kSliderWidth, kButtonHeight, bottomY, 12);
    for (HWND button : toolButtons_) {
        place(button, kToolWidth, kButtonHeight, bottomY, kToolGap);
    }

    if (canvas_) {
        ::MoveWindow(canvas_, 0, kBarHeight, width,
                     (std::max)(1, height - kBarHeight * 2), TRUE);
    }
}

void EditorWindow::RefreshToolbarState() {
    if (undoButton_) ::EnableWindow(undoButton_, !undoStack_.empty());
    if (redoButton_) ::EnableWindow(redoButton_, !redoStack_.empty());
    // Owner-drawn, so "which one is on" is not a control state to be set but
    // a repaint to be asked for; currentTool_ is the only copy of that fact.
    for (int i = 0; i < kToolCount; ++i) {
        if (toolButtons_[i]) ::InvalidateRect(toolButtons_[i], nullptr, TRUE);
    }
    if (swatch_) ::InvalidateRect(swatch_, nullptr, TRUE);

    // Owner-drawn, and it reads the registry when it paints, so refreshing
    // it is a repaint rather than a text assignment.
    if (pinButton_) ::InvalidateRect(pinButton_, nullptr, TRUE);
    UpdatePinTooltip();
}

void EditorWindow::UpdatePinTooltip() {
    if (!tooltips_ || !pinButton_) return;

    // With the words gone from the button face, the tooltip is the only
    // place the state is spelled out — so it has to be rewritten on every
    // toggle rather than set once at creation. A tooltip that still says
    // "Off" after you switched it on is worse than no tooltip: the button
    // is telling the truth and the label is contradicting it.
    const bool on = settings::GetBool(settings::key::kPinToScreen, false);
    const wchar_t* text =
        on ? L"Keep the Editor on Top: On — this window stays above other "
             L"windows.  Click to turn off."
           : L"Keep the Editor on Top: Off — this window behaves normally.  "
             L"Click to turn on.";

    TOOLINFOW info{};
    info.cbSize   = sizeof(info);
    info.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd     = hwnd_;
    info.uId      = reinterpret_cast<UINT_PTR>(pinButton_);
    info.lpszText = const_cast<LPWSTR>(text);
    ::SendMessageW(tooltips_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
}

std::vector<EditorWindow*>& EditorWindow::LiveEditors() {
    // Deliberately never destroyed, and this is not tidiness lost to
    // laziness — it is a use-after-free avoided.
    //
    // App is itself a function-local static, constructed before the first
    // editor ever exists, so this vector is constructed AFTER it. Statics
    // are destroyed in reverse order of construction, so at exit this one
    // would go first — and then ~App destroys editors_, running
    // ~EditorWindow, which erases from a vector that no longer exists.
    // Quitting from the tray with an editor still open reaches it.
    //
    // One leaked vector of pointers for the life of the process is the
    // cheapest correct answer; the alternative is teaching two independent
    // statics about each other's lifetimes.
    static auto* editors = new std::vector<EditorWindow*>();
    return *editors;
}

void EditorWindow::PinSettingChanged() {
    // Called from here when the editor's own toggle is clicked, and from
    // App when the tray row is. One setting, two switches, and no editor
    // left showing the state the other one just changed.
    for (EditorWindow* editor : LiveEditors()) {
        if (!editor || !editor->pinButton_) continue;
        ::InvalidateRect(editor->pinButton_, nullptr, TRUE);
        editor->UpdatePinTooltip();
        // Live, not on next open. The point of a switch in the window is
        // seeing the window obey it.
        editor->ApplyAlwaysOnTop();
    }
}

COLORREF EditorWindow::ActiveColour() const {
    // One colour again. This returned a separate black for the Redact tool,
    // which existed because a redaction defaulting to bright green is
    // absurd. A Shift-filled rectangle is drawn in whatever you picked, the
    // same as every other mark, so there is nothing left to special-case —
    // and one swatch that always means one thing is worth more than the
    // convenience it replaced.
    return currentColour_;
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
        draft_.colour    = ActiveColour();
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

        // Moved, not copied. A long pen stroke carries every sampled point
        // — hundreds of KB for a canvas-filling scribble — and this ran once
        // per completed stroke. hasDraft_ is cleared on the next line and
        // every reader of draft_ is guarded by it, so the moved-from state is
        // never observed.
        Annotation shape = std::move(draft_);
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

        if (shape.tool == Tool::Rectangle || shape.tool == Tool::Ellipse) {
            // Read at mouse-UP, like Lift's Shift, so the decision is the
            // one you were holding when you let go rather than the one you
            // happened to start with. Both modifiers work the same way for
            // the same reason.
            shape.filled = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
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

        const bool isCallout = (shape.tool == Tool::Callout);

        Snapshot();
        annotations_.push_back(std::move(shape));
        selectedIndex_ = static_cast<int>(annotations_.size()) - 1;
        RefreshToolbarState();
        ::InvalidateRect(canvas_, nullptr, FALSE);

        if (isCallout) {
            // The arrow is already committed and already on the undo stack.
            // Typing now fills in its label; cancelling leaves the arrow,
            // because you drew that part deliberately and losing it for
            // changing your mind about the words would be a surprise.
            //
            // The field opens where the text will be drawn, so the glyphs do
            // not jump on commit — the same rule the plain Text tool follows.
            // Set AFTER the field exists, and only if it does. Set before,
            // it would be consumed by the CommitTextEntry that BeginTextEntry
            // opens with, and it would be left pointing at this arrow if
            // CreateWindowEx failed — so the next ordinary label typed
            // anywhere on the canvas would be swallowed by this callout.
            const int arrowIndex = selectedIndex_;
            Graphics measure(canvas_);
            const RectD label = annotations_[arrowIndex].CalloutLabelBox(&measure);
            BeginTextEntry({ label.MinX(), label.MinY() });
            if (textEntryActive_) calloutIndex_ = arrowIndex;
        }
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
        if (LOWORD(wParam) == IDC_TEXTEDIT) {
            if (HIWORD(wParam) == EN_KILLFOCUS) { CommitTextEntry(); return 0; }
            if (HIWORD(wParam) == EN_CHANGE)    { RepositionCalloutField(); return 0; }
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
    // Reused, not reallocated on every mouse-move. Only grows; see
    // paintBuffer_ in the header. The BitBlt out at the end reads only the
    // top-left width x height, so a larger buffer is harmless, and FillRect
    // below clears the part that is used.
    // Matched exactly to the canvas, not grown to a high-water mark. The
    // canvas only changes size when the window is resized, so this
    // reallocates on resize and never during a drag — and unlike a
    // grow-only buffer it does not stay at the size of the largest the window
    // has ever been after the user maximises and restores.
    if (!paintBuffer_ || paintBuffer_->Width() != width ||
        paintBuffer_->Height() != height) {
        paintBuffer_.reset();
        paintBuffer_ = Bitmap::Create(width, height);
    }
    if (!paintBuffer_ || !paintBuffer_->MemoryDC()) return;
    HDC target = paintBuffer_->MemoryDC();

    ::FillRect(target, &client, ::GetSysColorBrush(COLOR_APPWORKSPACE));

    const RECT   rect  = ImageRect();
    const double scale = ImageScale();

    if (image_->MemoryDC()) {
        const int destWidth  = util::RectWidth(rect);
        const int destHeight = util::RectHeight(rect);

        if (destWidth == image_->Width() && destHeight == image_->Height()) {
            // Shown at 1:1 — nothing to scale, so blit the original and hold
            // no cache at all. This is why a small capture costs no extra
            // memory, and it is also why the wobble was never visible on one.
            scaledImage_.reset();
            ::BitBlt(target, rect.left, rect.top, destWidth, destHeight,
                     image_->MemoryDC(), 0, 0, SRCCOPY);
        } else {
            // Scaled ONCE, with the good resampler, and kept. The size is the
            // cache key, so a window resize rebuilds it and nothing else does.
            if (!scaledImage_ || scaledImage_->Width() != destWidth ||
                scaledImage_->Height() != destHeight) {
                // Released before allocating, so a resize never holds two.
                scaledImage_.reset();
                scaledImage_ = Bitmap::Create(destWidth, destHeight);

                if (scaledImage_ && scaledImage_->MemoryDC()) {
                    // HALFTONE averages the source pixels that map to each
                    // destination pixel. SetBrushOrgEx after it is required,
                    // not optional — without it GDI misaligns the brush it
                    // uses internally for the filter.
                    ::SetStretchBltMode(scaledImage_->MemoryDC(), HALFTONE);
                    ::SetBrushOrgEx(scaledImage_->MemoryDC(), 0, 0, nullptr);
                    ::StretchBlt(scaledImage_->MemoryDC(), 0, 0, destWidth, destHeight,
                                 image_->MemoryDC(), 0, 0,
                                 image_->Width(), image_->Height(), SRCCOPY);
                } else {
                    scaledImage_.reset();
                }
            }

            if (scaledImage_ && scaledImage_->MemoryDC()) {
                // A plain copy. No resampling happens during a drag at all
                // now, which is what makes the quality constant.
                ::BitBlt(target, rect.left, rect.top, destWidth, destHeight,
                         scaledImage_->MemoryDC(), 0, 0, SRCCOPY);
            } else {
                // The cache could not be allocated. Scale per paint rather
                // than show nothing — still with the good resampler, because
                // a slow canvas beats one whose text changes as you draw.
                ::SetStretchBltMode(target, HALFTONE);
                ::SetBrushOrgEx(target, 0, 0, nullptr);
                ::StretchBlt(target, rect.left, rect.top, destWidth, destHeight,
                             image_->MemoryDC(), 0, 0,
                             image_->Width(), image_->Height(), SRCCOPY);
            }
        }
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
        // bar taking space from all eight, the hint appears only while the
        // tool it describes is selected, and only while nothing is being
        // dragged — by the time a drag is under way the choice has been made,
        // and the label would just sit under the cursor.
        //
        // The alternative was a second toolbar button per variant — "Lift"
        // and "Cut", "Rectangle" and "Filled Rectangle". That is more
        // discoverable and costs three more buttons. This says the same
        // thing for no width at all.
        if (ToolHasShiftVariant(currentTool_) && !hasDraft_) {
            // One line per tool that has a modifier. A modifier nobody
            // knows about is a feature that does not exist, and there are
            // three of them now — Lift's cut, and fill on both closed
            // shapes — so the line is chosen by tool rather than hardcoded
            // to the only one that used to have one.
            const wchar_t* hint =
                (currentTool_ == Tool::Lift)
                    ? L"Drag to copy a piece  ·  Shift-drag to cut it out"
                    : L"Drag for an outline  ·  Shift-drag to fill it";

            Gdiplus::FontFamily family(L"Segoe UI");
            Font font(&family, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            RectF measured;
            graphics.MeasureString(hint, -1, &font,
                                   Gdiplus::PointF(0.0f, 0.0f), &measured);

            const REAL padX = 10.0f, padY = 5.0f;
            const REAL boxWidth  = measured.Width + padX * 2;
            const REAL boxHeight = measured.Height + padY * 2;

            // RIGHT-aligned, not centred, and the reason is the colour
            // popup. That popup is 204 x 90 and opens directly above the
            // swatch — which is the leftmost control on the bottom bar — so
            // it rises into the bottom-left of the canvas, the same band
            // this hint sits in. Centred, at the 700px minimum window width
            // the hint spanned x 207..492 while the popup spanned x 10..214:
            // seven pixels of overlap, and since the popup is a topmost
            // window it won, clipping the start of the sentence.
            //
            // Anchoring the two to OPPOSITE edges makes the clearance a
            // property of the layout rather than a coincidence that held at
            // the window sizes anyone happened to try, and widening the
            // window only ever adds more of it.
            //
            // The margin is not generous, and it shrank in 1.8.0: the
            // minimum window width came down from 700 to 540, so the hint
            // now starts around x 243 against a popup ending at 214 —
            // roughly 30px rather than 190. Still clear, and nothing here
            // scales with DPI (the hint font is UnitPixel, the popup
            // constants are raw pixels, the minimum is raw pixels), so it
            // holds. But anyone lowering kMinContentWidth further should
            // check this first: it is the next thing that breaks.
            //
            // It also puts the hint under the tool buttons, which is where
            // the click that summoned it happened.
            const REAL boxLeft = (std::max)(0.0f,
                                            static_cast<REAL>(width) - boxWidth - 12.0f);
            const REAL boxTop  = static_cast<REAL>(height) - boxHeight - 12.0f;

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
    if (colour != currentColour_) {
        currentColour_ = colour;
        editor_settings::SetColour(colour);
        if (swatch_) ::InvalidateRect(swatch_, nullptr, TRUE);
    }

    // Restyling the selection is independent of which colour the swatch was
    // editing: you picked a colour with a mark selected, so the mark takes
    // it — including recolouring a redaction you had already drawn.
    // Compared against the MARK's colour, not the swatch's. The old early
    // return keyed on the swatch, which got both cases wrong once there were
    // two colours behind it: re-picking the active colour with a
    // differently-coloured mark selected did nothing, and now that the
    // function no longer returns early it would instead push an undo entry
    // for assigning a colour that was already there.
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size()) &&
        annotations_[selectedIndex_].colour != colour) {
        SnapshotStyleChangeIfNeeded();
        annotations_[selectedIndex_].colour = colour;
        ::InvalidateRect(canvas_, nullptr, FALSE);
    }
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

    // Clamped for the same reason RepositionCalloutField clamps: a callout
    // whose label sits back from its tail can start at a negative x when
    // the arrow was drawn near the left edge, and the part that falls off
    // is the part being typed.
    const int fieldX = (std::max)(0L, origin.x);

    textEdit_ = ::CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  fieldX, origin.y, width, (std::max)(18, height),
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

void EditorWindow::RepositionCalloutField() {
    if (!textEntryActive_ || !textEdit_ || calloutIndex_ < 0) return;
    if (calloutIndex_ >= static_cast<int>(annotations_.size())) return;

    const Annotation& arrow = annotations_[calloutIndex_];
    if (arrow.tool != Tool::Callout) return;
    // Which side moves flipped when the label moved to the tail. The label
    // now sits BEHIND the start point, so an arrow travelling RIGHT puts
    // its label to the left of the tail — positioned by its right edge,
    // which means its left edge walks with every character typed. An arrow
    // travelling left puts the label to the right of the tail, anchored by
    // its left edge, and that never moves.
    if (arrow.end.x < arrow.start.x) return;

    const int length = ::GetWindowTextLengthW(textEdit_);
    std::wstring typed;
    if (length > 0) {
        typed.resize(static_cast<size_t>(length) + 1, L'\0');
        const int copied = ::GetWindowTextW(textEdit_, &typed[0], length + 1);
        typed.resize(static_cast<size_t>((std::max)(0, copied)));
    }

    // Measured on a copy. Writing the in-progress text onto the real mark
    // would draw the label twice — once by the annotation, once by the edit
    // control sitting on top of it.
    Annotation probe = arrow;
    probe.text = std::move(typed);

    Graphics measure(canvas_);
    const RectD box = probe.CalloutLabelBox(&measure);
    POINT origin = ToViewPoint({ box.MinX(), box.MinY() });
    // Clamped, because this one grows leftwards: a long label on a callout
    // near the left edge would walk the field off the canvas, and the part
    // that leaves is the part being typed.
    origin.x = (std::max)(0L, origin.x);
    ::SetWindowPos(textEdit_, nullptr, origin.x, origin.y, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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

    // Taken and cleared before either early return below, or a callout whose
    // label was left empty would keep claiming the next label typed
    // anywhere else on the canvas.
    const int callout = calloutIndex_;
    calloutIndex_ = -1;

    // Trim; a label of nothing but spaces is not a label.
    size_t first = value.find_first_not_of(L" \t\r\n");
    size_t last  = value.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        // Nothing typed. For a callout the arrow stays — it is already
        // committed and already on the undo stack — and for a plain label
        // there was never anything to commit.
        if (callout >= 0) ::InvalidateRect(canvas_, nullptr, FALSE);
        return;
    }
    value = value.substr(first, last - first + 1);

    if (callout >= 0) {
        // No second Snapshot: the arrow's push already took one, and taking
        // another here would make Ctrl+Z remove the words and leave the
        // arrow — two undo steps for what was one action.
        // Range AND identity. The index alone would happily write the label
        // onto whatever mark had come to occupy that slot.
        if (callout < static_cast<int>(annotations_.size()) &&
            annotations_[callout].tool == Tool::Callout) {
            annotations_[callout].text = std::move(value);
        }
        RefreshToolbarState();
        ::InvalidateRect(canvas_, nullptr, FALSE);
        return;
    }

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
    // A cancelled callout keeps its arrow. Esc is being used to say "not
    // those words", not "not that arrow", and the arrow is a separate,
    // already-undoable action.
    const bool wasCallout = calloutIndex_ >= 0;
    calloutIndex_ = -1;
    ::SetFocus(canvas_);
    ::DestroyWindow(field);
    if (wasCallout) ::InvalidateRect(canvas_, nullptr, FALSE);
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

    // DOWNWARDS, out of the window entirely.
    //
    // It used to open upwards, which is the only direction that guarantees
    // covering the thing you are working on: the swatch lives on the bottom
    // bar, so "above the swatch" is always over the canvas — over the
    // picture, in the corner, while you are choosing the colour you are
    // about to draw on it with. A popup is a top-level window and is under
    // no obligation to stay inside its parent, so below the swatch is
    // simply the desktop, and the capture stays visible the whole time.
    //
    // The swatch already grew a downward caret when it became owner-drawn,
    // so this is also the direction it has been claiming to open in.
    int left = swatchRect.left;
    int top  = swatchRect.bottom + 4;

    // Unless there is no room down there. Clamped against the WORK AREA of
    // the monitor the swatch is on, not the primary one and not the full
    // monitor rectangle — a window dragged to the bottom of a secondary
    // screen, or sitting above the taskbar, would otherwise open its picker
    // behind the taskbar or off the end of the desk.
    // From the SWATCH, not the frame. An editor straddling two screens is
    // "mostly on" whichever holds more of the window, which need not be the
    // one the bottom-left corner — and therefore the picker — is on.
    HMONITOR monitor = ::MonitorFromRect(&swatchRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && ::GetMonitorInfoW(monitor, &info)) {
        if (top + height > info.rcWork.bottom) {
            // Flip back above the swatch, which is where it always was.
            // Covering the canvas is the fallback now rather than the rule.
            top = swatchRect.top - height - 4;
        }
        left = (std::max)(static_cast<int>(info.rcWork.left),
                          (std::min)(left, static_cast<int>(info.rcWork.right) - width));
        top  = (std::max)(static_cast<int>(info.rcWork.top), top);
    }

    colourPopup_ = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPopupClass, L"",
                                     WS_POPUP | WS_BORDER,
                                     left, top,
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
                                  editor_settings::kPresetColours[index] == ActiveColour();
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
    choose.rgbResult    = ActiveColour();
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
    FlashTitle(L"Copied");
}

void EditorWindow::SaveAsPng() {
    auto flattened = Flatten();
    if (!flattened) { ::MessageBeep(MB_ICONWARNING); return; }

    std::vector<BYTE> png = flattened->EncodePng();
    if (png.empty()) { ::MessageBeep(MB_ICONWARNING); return; }

    MediaFolder& folder = MediaFolder::Screenshots();
    folder.EnsureDirectoryExists();

    std::wstring name = L"SnipTextProUltra " + util::FileNameTimestamp() + L".png";
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
        ::MessageBoxW(hwnd_, L"That file couldn't be written.", L"SnipTextProUltra",
                      MB_OK | MB_ICONWARNING);
        return;
    }

    // Checked, because a title flashing "Saved" over a truncated file is
    // worse than an error. A full disk, a volume pulled mid-save, or an AV
    // hook can all take the write after CreateFileW succeeded — and this is
    // the interactive Save, so the user is standing right there and can
    // choose somewhere else.
    DWORD written = 0;
    if (!::WriteFile(file.get(), png.data(), static_cast<DWORD>(png.size()),
                     &written, nullptr) ||
        written != png.size()) {
        ::MessageBoxW(hwnd_, L"The file couldn't be written completely. "
                             L"It may be incomplete — try saving somewhere else.",
                      L"SnipTextProUltra", MB_OK | MB_ICONWARNING);
        return;
    }
    FlashTitle(L"Saved");
}

void EditorWindow::FlashTitle(const wchar_t* note) {
    ::KillTimer(hwnd_, kTitleFlashTimer);
    ::SetWindowTextW(hwnd_, note);
    ::SetTimer(hwnd_, kTitleFlashTimer, kTitleFlashMs, nullptr);
}
