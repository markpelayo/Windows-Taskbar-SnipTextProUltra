#include "EditorWindow.h"

#include "EditorSettings.h"
#include "Hotkeys.h"
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
constexpr const wchar_t* kSliderClass = L"SnipTextProUltraWidthSlider";

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
// 2000, up from 1200. 1.2s reads as a flicker rather than a message — long
// enough to notice something changed, too short to actually read the word,
// which is the worst of both. Two seconds is the shortest interval that can
// be read without hurrying.
constexpr UINT     kTitleFlashMs    = 2000;

// The fake button press that answers a keyboard shortcut.
//
// 150ms is chosen against human reaction time rather than against taste: a
// press shorter than about 100ms can be missed entirely between saccades,
// and anything past ~250ms starts to read as the button being stuck. This is
// the same range Windows itself uses for the visual answer to a keyboard
// space-bar press on a focused button.
constexpr UINT_PTR kButtonFlashTimer = 2;
constexpr UINT     kButtonFlashMs    = 150;

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

// Constrains `to` to the nearest 45-degree ray out of `from`.
//
// PROJECTION rather than rotation: the snapped point is where the cursor
// falls perpendicular onto the chosen ray, so the end of the line stays
// beside the pointer instead of swinging away from it at a fixed radius.
// Dragging roughly east gives end.x tracking the cursor with end.y pinned —
// which is what "hold Shift for a straight line" is expected to feel like.
PointD SnapToAxis(PointD from, PointD to) {
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    // Below about a pixel there is no direction to snap to, and atan2 of
    // nearly-zero is noise that would make the line flick between axes.
    if (std::hypot(dx, dy) < 1.0) return to;

    // kPi is the file-scope one above; redeclaring it here shadowed it,
    // which /W4 reports as C4459 and /WX turns into a build failure.
    constexpr double kStep = kPi / 4.0;   // 45 degrees, so all eight rays
    const double snapped = std::round(std::atan2(dy, dx) / kStep) * kStep;

    const double ux = std::cos(snapped);
    const double uy = std::sin(snapped);
    // The projection is never negative: `snapped` is the NEAREST ray, so the
    // vector is always within 22.5 degrees of it.
    const double length = dx * ux + dy * uy;
    return PointD{ from.x + length * ux, from.y + length * uy };
}

// Constrains a dragged rectangle to 1:1 — a square, or a circle for the
// ellipse.
//
// The side is the LARGER of the two spans, which is what every drawing app
// does: the shape grows to contain the drag rather than shrinking to fit
// inside it, so it keeps up with the pointer instead of lagging behind the
// dominant axis. The signs are preserved, so it still opens in whichever
// direction you are pulling.
PointD SquareOff(PointD from, PointD to) {
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double side = (std::max)(std::fabs(dx), std::fabs(dy));
    return PointD{ from.x + (dx < 0.0 ? -side : side),
                   from.y + (dy < 0.0 ? -side : side) };
}

// Clamps a region to bounds, keeping it square if it arrived square.
//
// Both places that clamp a region — the Lift branch below and ApplyCrop —
// can now be handed a Shift-squared selection, and both could therefore
// hand back a rectangle: the user holds Shift, sees a square marquee, lets
// go, and gets something that is not a square. Clamping is invisible in the
// common case and only bites when the drag runs into the grey letterbox, so
// the bug would have been rare, intermittent and baffling.
//
// Squareness is kept by SHRINKING to the shorter side, never growing to the
// longer one. Growing would push the region back outside the bounds we were
// just asked to stay inside, which is the entire point of clamping.
//
// `anchor` is where the drag STARTED — the one corner the user is holding
// still, and therefore the one corner that must not move. Shrinking has to
// know it. The first version of this function always shrank the far edges
// away from the top-left, which is right only when the drag went down and to
// the right; drag up-and-left into the letterbox and it would trim the corner
// the user was holding and slide the square somewhere they never pointed at.
// That is the same class of bug this function exists to fix — a square
// marquee that commits as something else — just expressed as a position error
// instead of an aspect-ratio one.
//
// The anchor is one of the two corners of the un-normalised drag, so
// comparing it to the region's own midpoint says which edges are fixed
// without needing to know anything else about the gesture.
RectD ClampRegion(const RectD& region, const RectD& bounds, bool keepSquare,
                  PointD anchor) {
    double x0 = (std::max)(bounds.MinX(), (std::min)(region.MinX(), bounds.MaxX()));
    double y0 = (std::max)(bounds.MinY(), (std::min)(region.MinY(), bounds.MaxY()));
    double x1 = (std::max)(bounds.MinX(), (std::min)(region.MaxX(), bounds.MaxX()));
    double y1 = (std::max)(bounds.MinY(), (std::min)(region.MaxY(), bounds.MaxY()));

    if (keepSquare) {
        const double side = (std::min)(x1 - x0, y1 - y0);
        // side <= the clamped extent on both axes, so moving an edge inward
        // by (extent - side) can never leave the bounds we just clamped to.
        if (anchor.x > region.MidX()) x0 = x1 - side; else x1 = x0 + side;
        if (anchor.y > region.MidY()) y0 = y1 - side; else y1 = y0 + side;
    }
    return RectD{ x0, y0, x1 - x0, y1 - y0 };
}

// Where a drag's end point lands, given the tool and the modifier keys as
// they are right now.
//
// One function rather than three copies. It is called from the mouse-move
// that is drawing, from Shift or Ctrl changing state mid-drag, and again at
// mouse-up — and the whole promise of the feature is that those three agree.
// They agreed by inspection before; now they agree by construction.
PointD ResolveDragEnd(Tool tool, PointD start, PointD raw) {
    if ((::GetKeyState(VK_SHIFT) & 0x8000) == 0) return raw;
    if (ToolSnapsToAxis(tool))        return SnapToAxis(start, raw);
    if (ToolConstrainsToSquare(tool)) return SquareOff(start, raw);
    return raw;
}

// Whether the drag in hand is being squared off right now. The clamps need
// to know, and asking them to re-derive it from the key state would be a
// second reading that can disagree with the first.
bool SquaringNow(Tool tool) {
    return ToolConstrainsToSquare(tool) && (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool FillFromModifiers(Tool tool) {
    return ToolCanFill(tool) && (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

// Whether a Lift should leave the source where it was.
//
// Ctrl-drag copies, plain drag moves — and that is the inversion in 1.9.3.
// It cost an established behaviour, so it is worth saying why:
//
//   1. Shift had to come free. Shift now squares off on every tool that has
//      a shape, and Lift cannot be the one tool where it means "cut".
//   2. Ctrl-drag-to-copy is what File Explorer does, and what every list,
//      canvas and file manager on this operating system does. Muscle memory
//      already exists; this borrows it rather than competing with it.
//   3. Plain drag MOVES things. That is what dragging is. Lifting a piece
//      out and leaving a duplicate behind was the surprising default, even
//      though it was the safer one.
//
// The lost safety is real and is answered by Ctrl+Z, which was already
// undoing the lift as one step — blanking the source adds nothing new to
// undo, because the source is blanked non-destructively and the capture
// underneath is never written to.
bool LiftKeepsSource(Tool tool) {
    return ToolCopiesWithCtrl(tool) && (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
}


// Draws into an off-screen bitmap and blits it once, on destruction.
//
// This is the whole of the "blinking" fix, and it is worth being precise
// about what the blink was: not animation — there is none anywhere in this
// program — but the compositor showing a half-finished paint. Every one of
// these surfaces was painted in layers straight to the screen: the slider
// filled its background, then the track, then the filled part, then the
// thumb, and the eye caught the intermediate states as a flash.
//
// One blit replaces all of that. It is also strictly LESS work than before,
// because the overlapping fills now happen in memory where nothing has to be
// composited, and the screen is touched exactly once per paint.
//
// The viewport origin is shifted so callers keep drawing in the target's own
// coordinates and need to know nothing about the buffer.
class BufferedDC {
public:
    BufferedDC(HDC target, const RECT& area)
        : target_(target), area_(area),
          dc_(::CreateCompatibleDC(target)),
          bitmap_(::CreateCompatibleBitmap(target, util::RectWidth(area),
                                           util::RectHeight(area))) {
        if (dc_ && bitmap_) {
            previous_ = ::SelectObject(dc_.get(), bitmap_.get());
            ::SetViewportOrgEx(dc_.get(), -area.left, -area.top, nullptr);
        }
    }
    BufferedDC(const BufferedDC&) = delete;
    BufferedDC& operator=(const BufferedDC&) = delete;

    ~BufferedDC() {
        if (!usable()) return;
        ::SetViewportOrgEx(dc_.get(), 0, 0, nullptr);
        ::BitBlt(target_, area_.left, area_.top,
                 util::RectWidth(area_), util::RectHeight(area_),
                 dc_.get(), 0, 0, SRCCOPY);
        // The bitmap has to come out of the DC before either is destroyed.
        ::SelectObject(dc_.get(), previous_);
    }

    // False if either allocation failed. Callers fall back to the target
    // directly — a flickering control beats a blank one.
    bool usable() const { return dc_ && bitmap_; }
    HDC  dc(HDC fallback) const { return usable() ? dc_.get() : fallback; }

private:
    HDC          target_;
    RECT         area_;
    ScopedDC     dc_;
    ScopedBitmap bitmap_;
    HGDIOBJ      previous_ = nullptr;
};

// A rounded rectangle as a GDI+ path: four arcs and the lines between them.
//
// GDI has RoundRect and it is not usable here. GDI does not antialias, so a
// GDI rounded corner is a staircase — visibly worse than the square corner
// it replaces. GDI+ is already linked and already initialised for
// annotations, so this costs a different drawing call rather than a new
// dependency.
Gdiplus::GraphicsPath* MakeRoundedPath(Gdiplus::GraphicsPath* path,
                                       const Gdiplus::RectF& box, REAL radius) {
    const REAL d = radius * 2.0f;
    path->Reset();
    path->AddArc(box.X,                 box.Y,                  d, d, 180.0f, 90.0f);
    path->AddArc(box.X + box.Width - d, box.Y,                  d, d, 270.0f, 90.0f);
    path->AddArc(box.X + box.Width - d, box.Y + box.Height - d, d, d,   0.0f, 90.0f);
    path->AddArc(box.X,                 box.Y + box.Height - d, d, d,  90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

// One face for every icon button on either bar. The top row and the bottom
// row are the same kind of control at the same size, so they are drawn by
// the same function rather than by three near-copies that drift apart the
// first time one of them is adjusted.
//
// `active` is "this is switched on" — the selected tool, or Pin when it is
// enabled. It is the only state that survives letting go of the mouse, so
// it gets a heavier accent outline: at 34px a hairline of blue is easy to
// miss across a desk.
void DrawIconButtonFace(HDC dc, const RECT& box, bool active, bool pressed) {
    // The corners this leaves uncovered have to be SOMETHING, and owner-draw
    // hands over a DC with no promise about what is already in it. The frame
    // class paints itself COLOR_BTNFACE, so that is what the toolbar behind
    // a button is, and filling the square first is what makes the rounded
    // shape read as a button on a bar rather than a button on a smear.
    ::FillRect(dc, &box, ::GetSysColorBrush(COLOR_BTNFACE));

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    // Pixel offset matters at this size: without it a one-pixel border
    // straddles the pixel grid and comes out as two half-covered greys
    // instead of one line.
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const int height = util::RectHeight(box);
    // Derived from the control, so it stays proportionate if the bar is ever
    // made taller or the program is run at a scaling factor.
    const REAL radius = static_cast<REAL>((std::max)(3, height / 7));

    // Inset by half a pen width. A GDI+ outline is centred on the path, so
    // drawing on the exact bounds puts half the line outside the rectangle,
    // where the neighbouring button then paints over it.
    Gdiplus::RectF shape(static_cast<REAL>(box.left) + 0.5f,
                         static_cast<REAL>(box.top) + 0.5f,
                         static_cast<REAL>(util::RectWidth(box)) - 1.0f,
                         static_cast<REAL>(height) - 1.0f);

    Gdiplus::GraphicsPath path;
    MakeRoundedPath(&path, shape, radius);

    const Gdiplus::Color faceColour = (active || pressed)
        ? Gdiplus::Color(255, 204, 228, 246)
        : Gdiplus::Color(255, 253, 253, 253);
    SolidBrush face(faceColour);
    graphics.FillPath(&face, &path);

    const Gdiplus::Color edgeColour = active ? Gdiplus::Color(255, 0, 103, 192)
                                             : Gdiplus::Color(255, 195, 199, 204);
    // 1.6 rather than 2 for the active ring: antialiased, a two-pixel line
    // reads heavier than the old doubled GDI frame did, and the point was
    // emphasis rather than weight.
    Pen edge(edgeColour, active ? 1.6f : 1.0f);
    graphics.DrawPath(&edge, &path);
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

    case Tool::Crop: {
        // The photographer's crop mark: two overlapping L-shaped corners,
        // each overshooting the other so they read as a frame being closed
        // in rather than a rectangle. Distinct from Rectangle, which is a
        // closed outline, and from Lift, which is a dashed marquee.
        g.Line(6.5, 1.5, 6.5, 13.5);
        g.Line(6.5, 13.5, 18.5, 13.5);
        g.Line(1.5, 6.5, 13.5, 6.5);
        g.Line(13.5, 6.5, 13.5, 18.5);
        break;
    }

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
    // The whole capture, until the crop tool says otherwise.
    if (image_) crop_ = util::MakeRect(0, 0, image_->Width(), image_->Height());
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
        // DBLCLKS so a double-click on a mark can open its label.
        canvas.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        canvas.lpfnWndProc   = &EditorWindow::CanvasProc;
        canvas.hInstance     = ::GetModuleHandleW(nullptr);
        // Null, deliberately. A class cursor is applied by DefWindowProc
        // before the window ever gets a say, which is why the canvas used
        // to show a crosshair over everything including the marks you were
        // trying to grab. WM_SETCURSOR picks one per position instead.
        canvas.hCursor       = nullptr;
        canvas.hbrBackground = nullptr;
        canvas.lpszClassName = kCanvasClass;
        ::RegisterClassExW(&canvas);

        WNDCLASSEXW slider{};
        slider.cbSize        = sizeof(slider);
        slider.lpfnWndProc   = &EditorWindow::SliderProc;
        slider.hInstance     = ::GetModuleHandleW(nullptr);
        slider.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        slider.hbrBackground = nullptr;   // fully painted; a brush would flash
        slider.lpszClassName = kSliderClass;
        ::RegisterClassExW(&slider);

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
        // pictures reads as two bars that happen to be touching.
        //
        // No accent ring on Save, despite what this comment used to promise:
        // BS_DEFPUSHBUTTON's ring goes away under owner-drawing, and the
        // replacement was never wired up — WM_DRAWITEM's `active` is true
        // only for the selected tool and for Pin. Left as it is deliberately.
        // The bar is five buttons wide and Ctrl+S is on the shortcut list;
        // emphasis that has to be explained is not emphasis.
        undoButton_ = MakeButton(hwnd_, L"", IDC_UNDO, BS_OWNERDRAW);
        redoButton_ = MakeButton(hwnd_, L"", IDC_REDO, BS_OWNERDRAW);
        copyButton_ = MakeButton(hwnd_, L"", IDC_COPY, BS_OWNERDRAW);
        saveButton_ = MakeButton(hwnd_, L"", IDC_SAVE, BS_OWNERDRAW);

        swatch_ = MakeButton(hwnd_, L"", IDC_SWATCH, BS_OWNERDRAW);

        // Still needed: the tooltip control below comes from comctl32 even
        // though the slider no longer does.
        ::InitCommonControls();

        // Ours. It holds no value of its own — currentLineWidth_ is the
        // single copy, and the slider reads and writes that — so there is
        // no TBM_SETPOS/TBM_GETPOS round trip and no way for the control
        // and the editor to disagree about the stroke width.
        slider_ = ::CreateWindowExW(0, kSliderClass, L"", WS_CHILD | WS_VISIBLE,
                                    0, 0, 10, 10, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SLIDER)),
                                    ::GetModuleHandleW(nullptr), this);

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
        auto* itemInfo = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

        // Every branch below paints its whole rectangle in several passes —
        // background, then face, then outline, then glyph — and doing that
        // straight to the screen is what made the swatch flash when clicked.
        // Buffered once here rather than in each branch.
        BufferedDC buffer(itemInfo->hDC, itemInfo->rcItem);
        DRAWITEMSTRUCT buffered = *itemInfo;
        buffered.hDC = buffer.dc(itemInfo->hDC);
        DRAWITEMSTRUCT* item = &buffered;

        if (item->CtlID == IDC_SWATCH) {
            RECT box = item->rcItem;
            ::FillRect(item->hDC, &box, ::GetSysColorBrush(COLOR_BTNFACE));
            ::InflateRect(&box, -2, -3);

            Gdiplus::Graphics graphics(item->hDC);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

            Gdiplus::RectF shape(static_cast<REAL>(box.left) + 0.5f,
                                 static_cast<REAL>(box.top) + 0.5f,
                                 static_cast<REAL>(util::RectWidth(box)) - 1.0f,
                                 static_cast<REAL>(util::RectHeight(box)) - 1.0f);
            Gdiplus::GraphicsPath path;
            MakeRoundedPath(&path, shape,
                            static_cast<REAL>((std::max)(3, util::RectHeight(box) / 6)));

            const COLORREF swatchColour = ActiveColour();
            SolidBrush fill(Gdiplus::Color(255, GetRValue(swatchColour),
                                           GetGValue(swatchColour),
                                           GetBValue(swatchColour)));
            graphics.FillPath(&fill, &path);

            // A white swatch needs the outline to be visible at all against
            // the toolbar behind it.
            Pen border(Gdiplus::Color(255, 128, 128, 128), 1.0f);
            graphics.DrawPath(&border, &path);
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
            // A disabled button gets the plain face, never the blue one.
            //
            // This never came up before 1.9.4, because a real mouse click
            // clears the pushed state before WM_COMMAND runs — so the
            // EnableWindow(FALSE) that follows the last undo always landed on
            // a released button. The shortcut flash holds the button down
            // ACROSS that disable, which made a state reachable that a click
            // cannot produce: pressed and disabled at once, drawn as the blue
            // "switched on" face with a greyed-out glyph. Blue means the
            // selected tool or Pin being on, and a spent Undo button is
            // neither. Resolved here at the draw site rather than by
            // shortening the flash, because the face is what was wrong.
            DrawIconButtonFace(item->hDC, box, active && !disabled,
                               pressed && !disabled);

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
            // Reverted to baseTitle_, never to the CURRENT window text: a
            // second flash inside the revert window would otherwise latch
            // "Copied" permanently. baseTitle_ is not a one-time capture —
            // UpdateTitleForCrop rewrites it — which is exactly why this
            // stays correct when a crop lands mid-flash. It is recomputed,
            // not remembered.
            ::SetWindowTextW(hwnd_, baseTitle_.c_str());
        } else if (wParam == kButtonFlashTimer) {
            ReleaseFlashedButton();
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
        // Same reason, and it must be dropped WITHOUT sending BM_SETSTATE:
        // the timer could otherwise fire against a handle that is about to
        // be invalid. KillTimer, then forget the button.
        ::KillTimer(hwnd_, kButtonFlashTimer);
        ::KillTimer(hwnd_, kTitleFlashTimer);
        flashingButton_ = nullptr;
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
        if (toolButtons_[i]) ::InvalidateRect(toolButtons_[i], nullptr, FALSE);
    }
    if (swatch_) ::InvalidateRect(swatch_, nullptr, FALSE);

    // Owner-drawn, and it reads the registry when it paints, so refreshing
    // it is a repaint rather than a text assignment.
    if (pinButton_) ::InvalidateRect(pinButton_, nullptr, FALSE);
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

bool EditorWindow::PreTranslateMessage(const MSG& message) {
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) {
        return false;
    }
    if (!message.hwnd) return false;

    for (EditorWindow* editor : LiveEditors()) {
        // IsWindow as well as null: between WM_DESTROY and App's deferred
        // reap, a live entry can hold a handle that no longer exists.
        if (!editor || !editor->hwnd_ || !::IsWindow(editor->hwnd_)) continue;
        // The frame itself, or anything inside it. Keyboard messages only
        // go to the focused window, and focus only lives in the active
        // window's tree — so reaching here at all means this editor is the
        // one in front.
        if (message.hwnd != editor->hwnd_ && !::IsChild(editor->hwnd_, message.hwnd)) {
            continue;
        }
        return editor->HandleEditorKey(static_cast<UINT>(message.wParam),
                                       hotkeys::CurrentModifiers());
    }
    return false;
}

bool EditorWindow::HandleEditorKey(UINT key, UINT modifiers) {
    // While a label is being typed, the inline EDIT owns EVERY key, not
    // just Esc — and this guard has to come first for that reason.
    //
    // Nested inside the Esc test, it let any other binding through: this
    // is the one action for which a bare letter is a legal binding, since
    // it is never registered globally, so rebinding it to "T" and then
    // typing a word with a T in it would have destroyed the editor
    // mid-word. The old canvas handler could not do this, because the
    // canvas never saw keys while the field had focus; hoisting the
    // shortcut into the message loop is exactly what introduced the
    // possibility.
    if (textEntryActive_) {
        // Esc with the field open belongs to TextEditProc, which cancels
        // without committing. The exception is focus having left the
        // field without EN_KILLFOCUS to clear the flag — then nobody else
        // is going to handle it.
        if (key == VK_ESCAPE && modifiers == 0 && ::GetFocus() != textEdit_) {
            CancelTextEntry();
            return true;
        }
        return false;
    }

    // Esc backs out of the smallest outstanding thing first, and does so
    // whether or not the close binding is still on Esc — dropping a
    // selection is what the key means in a drawing surface, not a feature
    // of the shortcut.
    if (key == VK_ESCAPE && modifiers == 0 && selectedIndex_ >= 0) {
        ClearSelection();
        return true;
    }

    if (hotkeys::Matches(hotkeys::Action::CloseEditor, key, modifiers)) {
        // Posted rather than sent: this is running inside the message
        // loop, and WM_CLOSE tears the window down. Let the loop finish
        // with this message before that starts.
        //
        // No save prompt. Nothing here has been written to disk, Copy and
        // Save are one keystroke each, and the binding can be removed in
        // Change Keyboard Shortcut.
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}

void EditorWindow::PinSettingChanged() {
    // Called from here when the editor's own toggle is clicked, and from
    // App when the tray row is. One setting, two switches, and no editor
    // left showing the state the other one just changed.
    for (EditorWindow* editor : LiveEditors()) {
        if (!editor || !editor->pinButton_) continue;
        ::InvalidateRect(editor->pinButton_, nullptr, FALSE);
        editor->UpdatePinTooltip();
        // Live, not on next open. The point of a switch in the window is
        // seeing the window obey it.
        editor->ApplyAlwaysOnTop();
    }
}

COLORREF EditorWindow::ActiveColour() const {
    // One colour again. This returned a separate black for the Redact tool,
    // which existed because a redaction defaulting to bright green is
    // absurd. A Ctrl-filled rectangle is drawn in whatever you picked, the
    // same as every other mark, so there is nothing left to special-case —
    // and one swatch that always means one thing is worth more than the
    // convenience it replaced.
    return currentColour_;
}

void EditorWindow::RefreshDraftForModifiers() {
    // Only meaningful mid-draw. A move, a resize or a label swing has no
    // modifier behaviour, and there is no draft to re-resolve.
    if (dragMode_ != DragMode::Drawing || !hasDraft_) return;

    // Against the last pointer position, because the pointer has not moved
    // — that is the whole point of this function. Without it the preview
    // only caught up on the next mouse-move, so pressing Shift and then
    // releasing the button without moving committed a shape that did not
    // match the last frame drawn.
    const PointD resolved = ResolveDragEnd(draft_.tool, draft_.start, dragLastPoint_);
    const bool   fill     = FillFromModifiers(draft_.tool);
    if (resolved.x == draft_.end.x && resolved.y == draft_.end.y &&
        fill == draft_.filled) {
        return;   // nothing changed; do not repaint for a key we do not use
    }

    draft_.end    = resolved;
    draft_.filled = fill;
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::ReturnFocusToCanvas() {
    if (canvas_ && !textEntryActive_) ::SetFocus(canvas_);
}

// --- the width slider ------------------------------------------------------

LRESULT CALLBACK EditorWindow::SliderProc(HWND hwnd, UINT message,
                                          WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<EditorWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<EditorWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    return self->OnSliderMessage(hwnd, message, wParam, lParam);
}

// The usable travel of the thumb CENTRE, inset by its own radius at each
// end. Without the inset the thumb would hang off both ends at the
// extremes, and the value under the pointer would not be the value drawn.
namespace {
constexpr int kThumbRadius = 7;
}

double EditorWindow::SliderValueForX(int x) const {
    if (!slider_) return currentLineWidth_;
    RECT client{};
    ::GetClientRect(slider_, &client);
    const int left  = kThumbRadius;
    const int right = (std::max)(left + 1, util::RectWidth(client) - kThumbRadius);
    const double t  = (std::min)(1.0, (std::max)(0.0,
        static_cast<double>(x - left) / static_cast<double>(right - left)));
    return kSliderMin + t * (kSliderMax - kSliderMin);
}

int EditorWindow::SliderXForValue(double value) const {
    if (!slider_) return kThumbRadius;
    RECT client{};
    ::GetClientRect(slider_, &client);
    const int left  = kThumbRadius;
    const int right = (std::max)(left + 1, util::RectWidth(client) - kThumbRadius);
    const double t  = (std::min)(1.0, (std::max)(0.0,
        (value - kSliderMin) / static_cast<double>(kSliderMax - kSliderMin)));
    return left + static_cast<int>(std::lround(t * (right - left)));
}

LRESULT EditorWindow::OnSliderMessage(HWND hwnd, UINT message,
                                      WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(hwnd, &paint);
        if (!dc) { ::EndPaint(hwnd, &paint); return 0; }

        RECT client{};
        ::GetClientRect(hwnd, &client);

        // Scoped, and that is not tidiness. ~Graphics calls
        // GdipDeleteGraphics, which touches the HDC — so a Graphics still
        // alive when EndPaint returns is using a DC that has been released.
        // The buffer has to be torn down inside the same scope, before
        // EndPaint, for the same reason.
        {
        BufferedDC buffer(dc, client);
        HDC paintDC = buffer.dc(dc);
        ::FillRect(paintDC, &client, ::GetSysColorBrush(COLOR_BTNFACE));

        Gdiplus::Graphics graphics(paintDC);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        // Everything is measured from the MIDDLE of the control, which is
        // the whole point of replacing the trackbar: a horizontal trackbar
        // without TBS_BOTH gets a downward-pointing thumb, and Windows
        // makes room for the point by pushing the channel above centre.
        const REAL midY  = static_cast<REAL>(util::RectHeight(client)) / 2.0f;
        const int  thumbX = SliderXForValue(currentLineWidth_);
        const REAL trackLeft  = static_cast<REAL>(kThumbRadius);
        const REAL trackRight = static_cast<REAL>(
            (std::max)(kThumbRadius + 1, util::RectWidth(client) - kThumbRadius));

        constexpr REAL kTrackThickness = 4.0f;

        // The unfilled remainder first, full width, then the filled part
        // over it. Drawing them as two abutting rounded bars would leave a
        // seam where the antialiased ends meet.
        SolidBrush rest(Gdiplus::Color(255, 205, 209, 214));
        graphics.FillRectangle(&rest, trackLeft, midY - kTrackThickness / 2.0f,
                               trackRight - trackLeft, kTrackThickness);

        SolidBrush filled(Gdiplus::Color(255, 0, 103, 192));
        graphics.FillRectangle(&filled, trackLeft, midY - kTrackThickness / 2.0f,
                               static_cast<REAL>(thumbX) - trackLeft, kTrackThickness);

        // The thumb last, so it covers both ends of the track it sits on.
        const REAL r  = static_cast<REAL>(kThumbRadius);
        const REAL cx = static_cast<REAL>(thumbX);
        SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
        Pen        ring(Gdiplus::Color(255, 0, 103, 192), 2.0f);
        graphics.FillEllipse(&white, cx - r, midY - r, r * 2, r * 2);
        graphics.DrawEllipse(&ring,  cx - r + 1.0f, midY - r + 1.0f,
                             r * 2 - 2.0f, r * 2 - 2.0f);
        }

        ::EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   // fully painted above

    case WM_LBUTTONDOWN:
        // SetCapture FIRST, then the flag — the order the canvas and the
        // region overlay both use. SetCapture on a window that already
        // holds the capture delivers WM_CAPTURECHANGED to it, and that
        // handler clears this flag; setting it first would let a swallowed
        // mouse-up leave the next drag dead on arrival.
        ::SetCapture(hwnd);
        draggingSlider_ = true;
        SetCurrentLineWidth(SliderValueForX(GET_X_LPARAM(lParam)), false);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE:
        if (!draggingSlider_) return 0;
        SetCurrentLineWidth(SliderValueForX(GET_X_LPARAM(lParam)), false);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (draggingSlider_) {
            draggingSlider_ = false;
            ::ReleaseCapture();
            // Written once, at the end of the gesture.
            editor_settings::SetLineWidth(currentLineWidth_);
            // Focus must come back or Delete and Esc silently stop working
            // on the selection.
            ReturnFocusToCanvas();
        }
        return 0;

    case WM_CAPTURECHANGED:
        // Capture can be taken away — Alt-Tab, a lock screen, a UAC prompt.
        // Without this the thumb keeps following the pointer afterwards.
        draggingSlider_ = false;
        return 0;

    case WM_MOUSEWHEEL: {
        const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (notches == 0) return 0;
        SetCurrentLineWidth((std::min)(static_cast<double>(kSliderMax),
                            (std::max)(static_cast<double>(kSliderMin),
                                       currentLineWidth_ + notches)));
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

// --- coordinate mapping ----------------------------------------------------

double EditorWindow::ImageScale() const {
    if (!canvas_ || !image_) return 1.0;
    RECT client{};
    ::GetClientRect(canvas_, &client);
    // Against the CROP, not the capture. Everything the canvas does is in
    // terms of the region currently being shown; the capture behind it may
    // be much larger and is none of the canvas's business.
    const double scale = (std::min)(1.0,
        (std::min)(static_cast<double>(util::RectWidth(client)) / (std::max)(1, CropWidth()),
                   static_cast<double>(util::RectHeight(client)) / (std::max)(1, CropHeight())));
    return (std::max)(scale, 0.0001);
}

RECT EditorWindow::ImageRect() const {
    RECT client{};
    if (canvas_) ::GetClientRect(canvas_, &client);
    const double scale = ImageScale();
    const int width  = static_cast<int>(CropWidth() * scale);
    const int height = static_cast<int>(CropHeight() * scale);
    const int left = (util::RectWidth(client) - width) / 2;
    const int top  = (util::RectHeight(client) - height) / 2;
    return util::MakeRect(left, top, left + width, top + height);
}

PointD EditorWindow::ToImagePoint(POINT view) const {
    const RECT   rect  = ImageRect();
    const double scale = ImageScale();
    // The crop origin is added back, so annotations are always stored in
    // ORIGINAL capture coordinates no matter how many times the view has
    // been cropped. That is what lets a crop be undone without touching a
    // single mark, and what stops repeated crops accumulating an offset.
    return PointD{ crop_.left + (view.x - rect.left) / scale,
                   crop_.top  + (view.y - rect.top)  / scale };
}

POINT EditorWindow::ToViewPoint(PointD image) const {
    const RECT   rect  = ImageRect();
    const double scale = ImageScale();
    POINT out{ rect.left + static_cast<int>((image.x - crop_.left) * scale),
               rect.top  + static_cast<int>((image.y - crop_.top)  * scale) };
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
        dragAnchor_    = point;
        dragPassedThreshold_ = false;
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

        // 2. The selected mark's LABEL. Before the general hit-test, so
        //    dragging the text swings it around its mark instead of
        //    dragging the whole object — the label is the only part with
        //    two possible meanings, and the specific one wins.
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
            const Annotation& selected = annotations_[selectedIndex_];
            Graphics measure(canvas_);
            // IsWithinCrop as well, or a mark cropped out of view keeps a
            // grabbable label out in the grey letterbox — the exact thing
            // that guard exists to prevent.
            if (selected.HasLabel() && IsWithinCrop(selected, &measure)) {
                const RectD labelBox = selected.LabelBox(&measure);
                if (util::PointInRectD(labelBox.MinX(), labelBox.MinY(),
                                       labelBox.MaxX(), labelBox.MaxY(),
                                       point.x, point.y)) {
                    // Where in the label it was grabbed. AimLabelAt centres
                    // the box on the point it is given, so without this the
                    // label teleports its middle under the pointer the
                    // instant you touch it anywhere off-centre.
                    labelGrabOffset_ = PointD{ point.x - labelBox.MidX(),
                                               point.y - labelBox.MidY() };
                    dragMode_ = DragMode::MovingLabel;
                    return 0;
                }
            }
        }

        // 3. The topmost annotation under the point, searched from the end.
        {
            Graphics measure(canvas_);
            for (int i = static_cast<int>(annotations_.size()) - 1; i >= 0; --i) {
                if (!IsWithinCrop(annotations_[i], &measure)) continue;
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

    case WM_MOUSEWHEEL: {
        // Forwarded, because a wheel message goes to the FOCUSED window and
        // the slider never takes focus — the canvas does. Windows 10 and 11
        // default "Scroll inactive windows when I hover over them" to on,
        // which delivers it to the slider directly, but that is a setting
        // and can be off. Then it arrives here instead, and without this it
        // would silently do nothing.
        if (slider_) {
            POINT cursor{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };   // screen
            RECT sliderRect{};
            ::GetWindowRect(slider_, &sliderRect);
            if (::PtInRect(&sliderRect, cursor)) {
                return ::SendMessageW(slider_, WM_MOUSEWHEEL, wParam, lParam);
            }
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        // The discoverable half of F2. A double-click on a mark opens its
        // label; on empty canvas it does nothing, rather than guessing.
        //
        // No CommitTextEntry up front. With the Text tool selected, the
        // first click of a double-click OPENS a field on empty canvas, and
        // committing here would close it blank before a character could be
        // typed. Committed inside the loop instead, once a mark is
        // actually hit.
        POINT view{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const PointD point = ToImagePoint(view);
        const double tolerance = kHitTolerance / ImageScale();
        Graphics measure(canvas_);
        for (int i = static_cast<int>(annotations_.size()) - 1; i >= 0; --i) {
            if (!IsWithinCrop(annotations_[i], &measure)) continue;
            if (annotations_[i].HitTest(point, tolerance, &measure)) {
                CommitTextEntry();
                selectedIndex_ = i;
                ::InvalidateRect(canvas_, nullptr, FALSE);
                BeginLabelEntry(i);
                return 0;
            }
        }
        return 0;
    }

    case WM_SETCURSOR: {
        // Only for the canvas itself. DefWindowProc sends WM_SETCURSOR to
        // the PARENT first and stops if the parent returns TRUE — so
        // without the wParam test this would answer on behalf of the
        // inline text field too, computing a cursor from the canvas point
        // underneath it and taking the EDIT control's I-beam away.
        if (reinterpret_cast<HWND>(wParam) != canvas_ ||
            LOWORD(lParam) != HTCLIENT) {
            break;
        }
        POINT cursor{};
        ::GetCursorPos(&cursor);
        ::ScreenToClient(canvas_, &cursor);
        ::SetCursor(CursorForPoint(cursor));
        return TRUE;
    }

    case WM_MOUSEMOVE: {
        if (dragMode_ == DragMode::None) {
            // Nothing is being dragged, but the answer still changes as
            // the pointer crosses a mark or a handle. WM_SETCURSOR alone
            // fires often enough in practice, and this makes it certain.
            ::SetCursor(CursorForPoint(POINT{ GET_X_LPARAM(lParam),
                                              GET_Y_LPARAM(lParam) }));
            return 0;
        }
        POINT view{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const PointD point = ToImagePoint(view);
        const double scale = ImageScale();

        // A move, a resize or a label swing has to travel before it takes
        // effect. Two reasons, and the second is the one that bit: the
        // stray mouse-move between the two clicks of a DOUBLE-click would
        // otherwise nudge the mark a pixel and bank an undo step for it,
        // and every click-to-select risked the same. Drawing is exempt —
        // it has its own too-small test at mouse-up.
        if (dragMode_ != DragMode::Drawing && !dragPassedThreshold_) {
            if (std::hypot(point.x - dragAnchor_.x, point.y - dragAnchor_.y)
                    < kMinimumDrag / scale) {
                return 0;
            }
            dragPassedThreshold_ = true;
        }

        switch (dragMode_) {
        case DragMode::Drawing: {
            // Shift constrains the geometry — 45 degrees on a line, 1:1 on
            // a closed shape. Ctrl fills. Two independent switches rather
            // than four behaviours to learn, which is why Ctrl+Shift needs
            // no explanation of its own.
            //
            // Both resolved through the same pair of functions the key
            // handlers and mouse-up use, so the preview and the committed
            // mark agree by construction rather than by inspection.
            draft_.end    = ResolveDragEnd(draft_.tool, draft_.start, point);
            draft_.filled = FillFromModifiers(draft_.tool);

            if (draft_.tool == Tool::Pen && !draft_.points.empty()) {
                const PointD& last = draft_.points.back();
                // Skipping near-duplicates stops a slow stroke accumulating
                // tens of thousands of points.
                if (std::hypot(point.x - last.x, point.y - last.y) > kPenPointGap / scale) {
                    draft_.points.push_back(point);
                }
            }
            break;
        }
        case DragMode::Moving:
            TakeDragSnapshotIfNeeded();
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
                annotations_[selectedIndex_].MoveBy(point.x - dragLastPoint_.x,
                                                    point.y - dragLastPoint_.y);
            }
            break;
        case DragMode::MovingLabel:
            TakeDragSnapshotIfNeeded();
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
                Graphics measure(canvas_);
                // Always snapped to one of the eight — there is no modifier
                // to hold, which is what the angle being an INDEX buys.
                annotations_[selectedIndex_].AimLabelAt(
                    PointD{ point.x - labelGrabOffset_.x,
                            point.y - labelGrabOffset_.y }, &measure);
            }
            break;

        case DragMode::Resizing:
            TakeDragSnapshotIfNeeded();
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
                Annotation& mark = annotations_[selectedIndex_];
                PointD target = point;
                // The same constraint when re-aiming a line by its end, and
                // for the same reason: it is the same gesture. The anchor is
                // whichever end is NOT being dragged.
                if (ToolSnapsToAxis(mark.tool) &&
                    (::GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    if (activeHandle_ == Handle::End)   target = SnapToAxis(mark.start, point);
                    if (activeHandle_ == Handle::Start) target = SnapToAxis(mark.end, point);
                }
                mark.Resize(activeHandle_, resizeOriginalRect_, target);
            }
            break;
        default:
            break;
        }

        dragLastPoint_ = point;
        ::InvalidateRect(canvas_, nullptr, FALSE);
        return 0;
    }

    case WM_CAPTURECHANGED:
        // Capture can be taken away — Alt-Tab, a lock screen, a UAC prompt.
        // Without this the mark, handle or label keeps following the pointer
        // on every later hover, long after the button came up.
        //
        // This also arrives on the ordinary path, because ReleaseCapture in
        // WM_LBUTTONUP sends it. That is why WM_LBUTTONUP reads the mode
        // into a local first: resetting it here must not be able to cancel a
        // drag that is in the middle of being committed.
        //
        // RegionOverlay::HandleMessage solves the identical problem with a
        // releasingCapture_ flag, and documented it there first — which did
        // not stop 1.9.0 shipping this bug nine hundred lines away. Two
        // idioms, one hazard: a Win32 call inside a handler can send
        // messages back into the same window before it returns.
        dragMode_ = DragMode::None;
        needsSnapshotBeforeDrag_ = false;
        dragPassedThreshold_ = false;
        // A genuinely interrupted drag leaves a draft that nothing will ever
        // commit, and it would keep being drawn. Safe on the normal path
        // too: WM_LBUTTONUP moves draft_ out unconditionally.
        hasDraft_ = false;
        ::InvalidateRect(canvas_, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        // The mode is read BEFORE the capture is released, and everything
        // below uses the local rather than dragMode_.
        //
        // ReleaseCapture SENDS WM_CAPTURECHANGED synchronously, and that
        // handler resets dragMode_ to None — so with the old order, by the
        // time this line was reached every completed drag looked like a
        // finished move and took the "nothing to commit" path below.
        // Nothing was ever committed: no arrow, no rectangle, no crop, no
        // pen stroke. The handler was added in 1.9.0 and broke all drawing.
        const DragMode ending = dragMode_;
        ::ReleaseCapture();

        POINT view{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const PointD point = ToImagePoint(view);
        const double scale = ImageScale();

        if (ending != DragMode::Drawing) {
            // Covers Moving, Resizing and MovingLabel alike: the snapshot
            // was taken on the first drag event past the threshold, and
            // there is nothing to commit.
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
        // Resolved here too, and identically, or releasing the button would
        // drop the shape back to the raw cursor position and undo the
        // constraint at the last instant — the one frame the user is
        // actually looking at.
        shape.end = ResolveDragEnd(shape.tool, shape.start, point);

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

        if (ToolCanFill(shape.tool)) {
            // CTRL, not Shift. Shift was moved to squaring the shape off,
            // which is what it means in every drawing application — and
            // fill is not a constraint, so it had no business there.
            shape.filled = FillFromModifiers(shape.tool);
        }

        if (shape.tool == Tool::Lift) {
            // The drag picked the region; it has not moved anywhere yet. The
            // piece is created sitting exactly on top of where it came from,
            // so the picture looks unchanged until it is dragged away — which
            // is what makes both variants read correctly:
            //
            //   plain drag   the source is blanked underneath at the moment
            //                of the lift, hidden by the piece sitting on top
            //                of it, and pulling the piece aside reveals the
            //                hole. A move.
            //   Ctrl-drag    the original stays put, and pulling the piece
            //                aside reveals it still there. A copy.
            //
            // Clamped to the CROP, not the whole capture. A drag that
            // starts in the grey letterbox maps to coordinates outside the
            // crop, and the capture still holds those pixels — so clamping
            // to the capture would let a lift carry cropped-away content
            // back into the exported file, where there is no clip to hide
            // it.
            // shape.start is still the drag's origin here — it is overwritten
            // from the clamped region a few lines below, so the anchor has to
            // be read before that.
            const RectD region = ClampRegion(shape.NormalizedRect(), CropRegion(),
                                             SquaringNow(shape.tool), shape.start);
            if (region.width < 1.0 || region.height < 1.0) {
                ::InvalidateRect(canvas_, nullptr, FALSE);
                return 0;
            }

            shape.source = region;
            shape.start  = { region.MinX(), region.MinY() };
            shape.end    = { region.MaxX(), region.MaxY() };

            // Blank unless Ctrl says to leave it. See LiftKeepsSource.
            if (!LiftKeepsSource(shape.tool)) {
                shape.blankSource = true;
                shape.blankColour = DominantEdgeColour(region);
            }
        }

        if (shape.tool == Tool::Crop) {
            // Intercepted before the push: a Crop is never a mark. It
            // changes what the editor is looking at and then gets out of
            // the way.
            // Only disarm if it actually did something. A drag too small
            // to be a crop is a mis-drag, and switching to Arrow behind
            // the user's back means their second attempt draws an arrow.
            // Clamped and squared HERE, with the drag anchor, rather than
            // left to ApplyCrop. ApplyCrop's own clamp knows nothing about
            // which corner the user was holding, so a Shift-drag that ran
            // into the letterbox would come back square but displaced. Doing
            // it here leaves ApplyCrop nothing to trim, which is what makes
            // its integer squaring a harmless sub-pixel tidy-up.
            const bool squared = SquaringNow(shape.tool);
            const RectD region = ClampRegion(shape.NormalizedRect(), CropRegion(),
                                             squared, shape.start);
            if (ApplyCrop(region, squared)) {
                // Back to Arrow. Crop is an action, not a mode — leaving
                // it armed means the next drag silently crops again,
                // which is the sort of thing you only discover after
                // losing work.
                SetCurrentTool(Tool::Arrow);
            }
            RefreshToolbarState();
            ::InvalidateRect(canvas_, nullptr, FALSE);
            return 0;
        }

        Snapshot();
        annotations_.push_back(std::move(shape));
        selectedIndex_ = static_cast<int>(annotations_.size()) - 1;
        RefreshToolbarState();
        ::InvalidateRect(canvas_, nullptr, FALSE);

        return 0;
    }

    // Shift or Ctrl changing state mid-drag, with the pointer still. Before
    // WM_KEYDOWN's Ctrl block below, which returns unconditionally and would
    // otherwise swallow a bare VK_CONTROL — and WM_KEYUP is not handled
    // anywhere else, so this is the only place a release can be seen.
    case WM_KEYUP:
        if (wParam == VK_SHIFT || wParam == VK_CONTROL) {
            RefreshDraftForModifiers();
            return 0;
        }
        break;

    case WM_KEYDOWN: {
        if (wParam == VK_SHIFT || wParam == VK_CONTROL) {
            RefreshDraftForModifiers();
            return 0;
        }

        const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift   = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;


        // Shortcuts live here rather than in an accelerator table precisely
        // so they stand down while the inline text control has focus: when a
        // label is being typed, this window proc never sees the keystroke, so
        // Ctrl+Z edits the text instead of undoing the drawing.
        if (control) {
            switch (wParam) {
            // FlashButton FIRST, then the command. Two reasons, and both
            // matter: it decides whether to flash from the button's enabled
            // state, which the command itself can change (the last undo
            // disables Undo); and Save can block on a dialog or a slow disk,
            // so the press wants to be on screen before that starts rather
            // than after it finishes.
            case 'Z':
                if (shift) { FlashButton(redoButton_); Redo(); }
                else       { FlashButton(undoButton_); Undo(); }
                return 0;
            case 'Y': FlashButton(redoButton_); Redo();            return 0;
            case 'C': FlashButton(copyButton_); CopyToClipboard(); return 0;
            case 'S': FlashButton(saveButton_); SaveAsPng();       return 0;

            // Layering. Ctrl+arrows rather than bare arrows, which are
            // taken by nudging below — in every editor a bare arrow key
            // moves the selection a pixel, and that is both the more
            // common need and the one people try first. Ctrl is also what
            // PowerPoint and the Adobe tools use for this.
            //
            // Up is towards the viewer, matching "bring forward". The
            // array is back-to-front, so forward is a HIGHER index.
            case VK_UP:   MoveSelection(+1, shift); return 0;
            case VK_DOWN: MoveSelection(-1, shift); return 0;
            }
            return 0;
        }

        switch (wParam) {
        case VK_DELETE:
        case VK_BACK:
            DeleteSelection();
            return 0;

        // F2 labels the selection — the Windows rename convention, and the
        // same key whether the mark is a rectangle getting a label or a
        // piece of text getting edited. Both are "the string you typed".
        case VK_F2:
            if (selectedIndex_ >= 0) BeginLabelEntry(selectedIndex_);
            return 0;

        // A pixel at a time, ten with Shift. In IMAGE pixels, not view
        // pixels: a nudge on a capture shown at half size should move the
        // mark one pixel in the file, not two.
        case VK_LEFT:  NudgeSelection(shift ? -10.0 :  -1.0, 0.0); return 0;
        case VK_RIGHT: NudgeSelection(shift ?  10.0 :   1.0, 0.0); return 0;
        case VK_UP:    NudgeSelection(0.0, shift ? -10.0 : -1.0); return 0;
        case VK_DOWN:  NudgeSelection(0.0, shift ?  10.0 :  1.0); return 0;
        // Esc is not handled here at all. It runs through
        // PreTranslateMessage, so it behaves the same whether the canvas,
        // a tool button or the slider has focus — see HandleEditorKey.
        }
        return 0;
    }

    case WM_COMMAND:
        // The inline edit control is a child of the canvas, so its
        // notifications arrive here rather than at the frame.
        if (LOWORD(wParam) == IDC_TEXTEDIT) {
            if (HIWORD(wParam) == EN_KILLFOCUS) { CommitTextEntry(); return 0; }
            if (HIWORD(wParam) == EN_CHANGE)    { RepositionLabelField(); return 0; }
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

        if (destWidth == CropWidth() && destHeight == CropHeight()) {
            // Shown at 1:1 — nothing to scale, so blit the original and hold
            // no cache at all. This is why a small capture costs no extra
            // memory, and it is also why the wobble was never visible on one.
            scaledImage_.reset();
            ::BitBlt(target, rect.left, rect.top, destWidth, destHeight,
                     image_->MemoryDC(), crop_.left, crop_.top, SRCCOPY);
        } else {
            // Scaled ONCE, with the good resampler, and kept. The size is the
            // cache key, so a window resize rebuilds it and nothing else does.
            // The crop is part of the cache key. Two different crops can
            // land on the same destination size, and without this the
            // canvas would keep showing the region it was scaled from.
            if (!scaledImage_ || scaledImage_->Width() != destWidth ||
                scaledImage_->Height() != destHeight ||
                !util::RectsEqual(scaledFrom_, crop_)) {
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
                                 image_->MemoryDC(), crop_.left, crop_.top,
                                 CropWidth(), CropHeight(), SRCCOPY);
                    scaledFrom_ = crop_;
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
                             image_->MemoryDC(), crop_.left, crop_.top,
                             CropWidth(), CropHeight(), SRCCOPY);
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

        // Clipped to the visible picture. Marks are stored in original
        // capture coordinates and a crop does not move or delete them, so
        // one that now sits outside the crop would otherwise be drawn on
        // the grey canvas beside the image — visible, unreachable and
        // wrong. Clipping is also what makes a mark straddling the edge
        // look cut off rather than floating.
        graphics.SetClip(Gdiplus::Rect(rect.left, rect.top,
                                       util::RectWidth(rect), util::RectHeight(rect)));

        // The crop origin belongs in here. Map() computes p*scale + offset,
        // and marks are stored in ORIGINAL capture coordinates — so without
        // subtracting the crop's top-left, every mark is displaced by
        // crop_.topLeft * scale down and right of the picture it belongs
        // to, while the selection outline and handles (which go through
        // ToViewPoint, and do subtract it) stay put. Marks would separate
        // from their own handles, and the canvas would disagree with the
        // exported file. Invisible until you crop from somewhere other than
        // the top-left corner, which is exactly the kind of bug that ships.
        const PointD offset{ rect.left - crop_.left * scale,
                             rect.top  - crop_.top  * scale };
        for (const Annotation& annotation : annotations_) {
            annotation.Draw(graphics, scale, offset, picture.get());
        }
        if (hasDraft_) draft_.Draw(graphics, scale, offset, picture.get());

        // Released before the chrome. The clip exists to stop marks
        // spilling past the picture; selection outlines sit 4px outside a
        // mark's box and handles half a handle beyond that, so leaving it
        // on would slice the grips off any mark touching the crop edge —
        // and the hint bar, which lives in the canvas corner rather than
        // the picture, would be clipped away entirely.
        graphics.ResetClip();

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

        // A modifier nobody knows about is a feature that does not exist,
        // and there are six tools with something to say now. Every other tool does one
        // thing and needs no explanation, so rather than a permanent status
        // bar taking space from all eight, the hint appears only while the
        // tool it describes is selected, and only while nothing is being
        // dragged — by the time a drag is under way the choice has been made,
        // and the label would just sit under the cursor.
        //
        // The alternative was a toolbar button per variant — "Lift" and
        // "Cut", "Rectangle", "Square" and "Filled Square". That is more
        // discoverable and costs five more buttons. This says the same
        // thing for no width at all.
        if (ToolHasCanvasHint(currentTool_) && !hasDraft_) {
            // One line per tool with something to explain. Every line names
            // Shift first and in the same position, because after 1.9.3 it
            // means the same thing on all six of them — and a hint bar that
            // reads the same way every time teaches the rule, not the line.
            const wchar_t* hint =
                (currentTool_ == Tool::Lift)
                    ? L"Drag to move a piece  ·  Shift for a square  ·  Ctrl to copy"
                : (currentTool_ == Tool::Crop)
                    // NOT "Ctrl+Z undoes it". Every other line puts a Ctrl
                    // DRAG MODIFIER in the third slot, so naming Ctrl here —
                    // on the one tool where Ctrl-drag does nothing — would
                    // invite the reader to try a gesture that has no effect.
                    // The reassurance is what matters and it is still true:
                    // the capture is never modified.
                    ? L"Drag to keep that area  ·  Shift for a square  ·  Nothing is lost"
                : ToolSnapsToAxis(currentTool_)
                    ? L"Drag to draw  ·  Shift-drag to snap to 45°"
                : (currentTool_ == Tool::Ellipse)
                    ? L"Drag for an outline  ·  Shift for a circle  ·  Ctrl to fill"
                    : L"Drag for an outline  ·  Shift for a square  ·  Ctrl to fill";

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

bool EditorWindow::ApplyCrop(const RectD& region, bool keepSquare) {
    if (!image_) return false;

    // Clamped to the CURRENT crop, so a drag can only ever narrow the view.
    // Widening would mean showing pixels the user has already cropped away,
    // which undo is for.
    const long left   = static_cast<long>(std::lround((std::max)(
        static_cast<double>(crop_.left), region.MinX())));
    const long top    = static_cast<long>(std::lround((std::max)(
        static_cast<double>(crop_.top), region.MinY())));
    long right  = static_cast<long>(std::lround((std::min)(
        static_cast<double>(crop_.right), region.MaxX())));
    long bottom = static_cast<long>(std::lround((std::min)(
        static_cast<double>(crop_.bottom), region.MaxY())));

    // A rounding tidy-up, nothing more. Two independently-rounded edges can
    // leave a square one pixel off square, and a crop is the one region a
    // user might actually measure.
    //
    // It anchors at top-left, which is only safe because the Shift-drag
    // caller has already clamped and squared the region against this same
    // crop using the drag anchor — so the clamps above trim nothing and the
    // shrink here is at most one pixel. Any future caller passing
    // keepSquare with an unclamped region wants ClampRegion first.
    if (keepSquare) {
        const long side = (std::min)(right - left, bottom - top);
        right  = left + side;
        bottom = top  + side;
    }

    // A floor in IMAGE pixels, not view pixels. Cropping a 4K capture to
    // eight pixels is a mis-drag every time, and the result is a window
    // that cannot be usefully undone from because there is nothing to see.
    constexpr long kMinimumCrop = 16;
    if (right - left < kMinimumCrop || bottom - top < kMinimumCrop) return false;

    // Nothing to do if it already is the crop — and taking a snapshot for a
    // no-op would put a dead step on the undo stack.
    RECT next{ left, top, right, bottom };
    if (util::RectsEqual(next, crop_)) return false;

    Snapshot();
    crop_ = next;

    // The cache was scaled from the old region.
    scaledImage_.reset();
    // A mark can easily be outside the new view, and a selection you cannot
    // see with handles you cannot reach is worse than none.
    selectedIndex_ = -1;

    UpdateTitleForCrop();
    ::InvalidateRect(canvas_, nullptr, FALSE);
    return true;
}

bool EditorWindow::IsWithinCrop(const Annotation& annotation,
                                Gdiplus::Graphics* measureWith) const {
    // A crop does not move or delete marks, so one that now lies entirely
    // outside the visible picture is still in the array — and without this
    // it would still be CLICKABLE, out in the grey letterbox beside the
    // image: the cursor would turn to the move shape over apparently empty
    // space, and a click would select a mark whose selection chrome is
    // then clipped away. Invisible and unreachable have to mean the same
    // thing.
    const RectD box = annotation.BoundingBox(measureWith);
    return box.MaxX() >= crop_.left && box.MinX() <= crop_.right &&
           box.MaxY() >= crop_.top  && box.MinY() <= crop_.bottom;
}

void EditorWindow::UpdateTitleForCrop() {
    if (!hwnd_) return;
    baseTitle_ = util::Format(L"Screenshot %d × %d", CropWidth(), CropHeight());
    ::SetWindowTextW(hwnd_, baseTitle_.c_str());
}

void EditorWindow::Snapshot() {
    undoStack_.push_back(EditorState{ annotations_, crop_ });
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

    redoStack_.push_back(EditorState{ annotations_, crop_ });
    annotations_ = std::move(undoStack_.back().annotations);
    crop_        = undoStack_.back().crop;
    undoStack_.pop_back();
    // The index may no longer refer to the same mark.
    selectedIndex_ = -1;
    // The crop may have changed, which invalidates the scaled cache and
    // the size in the title.
    scaledImage_.reset();
    UpdateTitleForCrop();
    RefreshToolbarState();
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::Redo() {
    if (CancelTextEntry()) { ::InvalidateRect(canvas_, nullptr, FALSE); return; }
    if (redoStack_.empty()) return;

    undoStack_.push_back(EditorState{ annotations_, crop_ });
    annotations_ = std::move(redoStack_.back().annotations);
    crop_        = redoStack_.back().crop;
    redoStack_.pop_back();
    selectedIndex_ = -1;
    scaledImage_.reset();
    UpdateTitleForCrop();
    RefreshToolbarState();
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::MoveSelection(int delta, bool toEnd) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(annotations_.size())) return;
    const int count = static_cast<int>(annotations_.size());
    if (count < 2) return;   // nothing to move past

    // The array IS the z-order: marks are drawn back to front in order, and
    // hit-testing walks it backwards so the topmost is found first. Moving a
    // mark in the array is the whole operation — there is no separate depth
    // to keep in step, which is why this cannot drift out of sync with what
    // is on screen.
    const int from = selectedIndex_;
    const int to   = toEnd ? (delta > 0 ? count - 1 : 0)
                           : (std::min)(count - 1, (std::max)(0, from + delta));
    if (from == to) return;

    Snapshot();
    Annotation moved = std::move(annotations_[from]);
    annotations_.erase(annotations_.begin() + from);
    annotations_.insert(annotations_.begin() + to, std::move(moved));
    // The selection follows the mark, not the slot. Anything else means the
    // second press of the same key moves a different mark.
    selectedIndex_ = to;

    RefreshToolbarState();
    ::InvalidateRect(canvas_, nullptr, FALSE);
}

void EditorWindow::NudgeSelection(double dx, double dy) {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(annotations_.size())) return;
    // Coalesced like a slider drag: holding an arrow key auto-repeats, and
    // one undo step per repeat tick would bury the stack.
    //
    // On its own clock rather than the restyle one. A shared timer folded a
    // colour change and a nudge half a second apart into a single undo
    // step, which is two separate decisions the user made and would expect
    // to take back separately. Changing which mark is being nudged also
    // starts a new step, for the same reason.
    const ULONGLONG now = ::GetTickCount64();
    if (now - lastNudgeAt_ > kStyleCoalesceMs || lastNudgeIndex_ != selectedIndex_) {
        Snapshot();
    }
    lastNudgeAt_    = now;
    lastNudgeIndex_ = selectedIndex_;
    annotations_[selectedIndex_].MoveBy(dx, dy);
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
        if (swatch_) ::InvalidateRect(swatch_, nullptr, FALSE);
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

void EditorWindow::SetCurrentLineWidth(double width, bool persist) {
    if (std::fabs(width - currentLineWidth_) < 0.001) return;
    currentLineWidth_ = width;
    if (persist) editor_settings::SetLineWidth(width);
    // The slider draws itself from currentLineWidth_, so anything that
    // changes the width has to tell it — including the paths that do not
    // come from the slider at all.
    if (slider_) ::InvalidateRect(slider_, nullptr, FALSE);

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

    // Clamped for the same reason RepositionLabelField clamps: a label
    // centred on a ray can start at a negative x when its mark is near the
    // left edge, and the part that falls off is the part being typed.
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

HCURSOR EditorWindow::CursorForPoint(POINT view) const {
    // Resolved in exactly the order WM_LBUTTONDOWN resolves a click, so
    // what the pointer promises is what the click will do. Any other order
    // and the cursor is a lie at the boundaries.
    auto load = [](const wchar_t* name) { return ::LoadCursorW(nullptr, name); };

    // Mid-gesture the answer is fixed: a drag does not change its mind
    // because the pointer wandered over something else on the way.
    if (dragMode_ == DragMode::Moving ||
        dragMode_ == DragMode::MovingLabel) return load(IDC_SIZEALL);
    if (dragMode_ == DragMode::Resizing) return CursorForHandle(activeHandle_);
    if (dragMode_ == DragMode::Drawing)  return load(IDC_CROSS);

    // 1. A handle of the selected mark, pointing the way it will stretch.
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
        for (const auto& entry : annotations_[selectedIndex_].Handles()) {
            const POINT handleView = ToViewPoint(entry.second);
            if (std::hypot(view.x - handleView.x, view.y - handleView.y) <= kHandleSize) {
                return CursorForHandle(entry.first);
            }
        }
    }

    // 2. The selected mark's label, which swings rather than moves — but
    //    the four-way cursor is still the honest answer, because what it
    //    does is reposition a thing by dragging it.
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(annotations_.size())) {
        const Annotation& selected = annotations_[selectedIndex_];
        if (selected.HasLabel() && canvas_ && image_) {
            Graphics measure(canvas_);
            // Falls THROUGH to the next step when the selected mark is
            // cropped out of view, rather than returning — there may still
            // be another mark under the pointer that should answer.
            if (IsWithinCrop(selected, &measure)) {
                const RectD labelBox = selected.LabelBox(&measure);
                const PointD labelPoint = ToImagePoint(view);
                if (util::PointInRectD(labelBox.MinX(), labelBox.MinY(),
                                       labelBox.MaxX(), labelBox.MaxY(),
                                       labelPoint.x, labelPoint.y)) {
                    return load(IDC_SIZEALL);
                }
            }
        }
    }

    // 3. Any mark under the pointer — this one would be picked up and
    //    moved, so say so before the button goes down rather than after.
    if (canvas_ && image_) {
        const PointD point = ToImagePoint(view);
        const double tolerance = kHitTolerance / ImageScale();
        Graphics measure(canvas_);
        for (int i = static_cast<int>(annotations_.size()) - 1; i >= 0; --i) {
            if (!IsWithinCrop(annotations_[i], &measure)) continue;
            if (annotations_[i].HitTest(point, tolerance, &measure)) return load(IDC_SIZEALL);
        }
    }

    // 3. Empty canvas. The crosshair is right for every tool that draws by
    //    dragging — it is a precision cursor and it says "this is where the
    //    mark starts". Text is the exception: it does not drag out a shape,
    //    it puts a caret down, and an I-beam is what a caret looks like
    //    before you place it.
    return load(currentTool_ == Tool::Text ? IDC_IBEAM : IDC_CROSS);
}

HCURSOR EditorWindow::CursorForHandle(Handle handle) {
    // The diagonal pair share a cursor because they share an axis of
    // travel; so do the other diagonal, the two sides and the two ends.
    switch (handle) {
    case Handle::TopLeft:
    case Handle::BottomRight: return ::LoadCursorW(nullptr, IDC_SIZENWSE);
    case Handle::TopRight:
    case Handle::BottomLeft:  return ::LoadCursorW(nullptr, IDC_SIZENESW);
    case Handle::Left:
    case Handle::Right:       return ::LoadCursorW(nullptr, IDC_SIZEWE);
    case Handle::Top:
    case Handle::Bottom:      return ::LoadCursorW(nullptr, IDC_SIZENS);
    case Handle::Start:
    case Handle::End:
        // A line's endpoints are not constrained to an axis — they go
        // wherever you put them — so the four-way move cursor is the
        // honest one, not a diagonal that implies a direction.
        return ::LoadCursorW(nullptr, IDC_SIZEALL);
    case Handle::None:
    default:                  return ::LoadCursorW(nullptr, IDC_CROSS);
    }
}

void EditorWindow::RepositionLabelField() {
    if (!textEntryActive_ || !textEdit_ || labelIndex_ < 0) return;
    if (labelIndex_ >= static_cast<int>(annotations_.size())) return;

    const Annotation& mark = annotations_[labelIndex_];
    // A Text mark grows rightwards from a fixed origin, so its field never
    // needs to move. A LABEL is centred on its ray, so its left edge walks
    // with every character typed in every direction except due east.
    if (mark.tool == Tool::Text) return;

    const int length = ::GetWindowTextLengthW(textEdit_);
    std::wstring typed;
    if (length > 0) {
        typed.resize(static_cast<size_t>(length) + 1, L'\0');
        const int copied = ::GetWindowTextW(textEdit_, &typed[0], length + 1);
        typed.resize(static_cast<size_t>((std::max)(0, copied)));
    }

    // Measured on a COPY. Writing the in-progress text onto the real mark
    // would draw the label twice — once by the annotation, once by the edit
    // control sitting on top of it — and would also mean an abandoned entry
    // had already changed the picture.
    Annotation probe = mark;
    probe.text = std::move(typed);

    Graphics measure(canvas_);
    const RectD box = probe.LabelBox(&measure);
    POINT origin = ToViewPoint({ box.MinX(), box.MinY() });
    // Clamped: a long label on a mark near the left edge would otherwise
    // walk the field off the canvas, and the part that leaves is the part
    // being typed.
    origin.x = (std::max)(0L, origin.x);
    origin.y = (std::max)(0L, origin.y);
    ::SetWindowPos(textEdit_, nullptr, origin.x, origin.y, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void EditorWindow::BeginLabelEntry(int index) {
    if (index < 0 || index >= static_cast<int>(annotations_.size())) return;

    // Everything read from the mark is COPIED out first, inside its own
    // scope. BeginTextEntry below opens with CommitTextEntry, which can
    // push a new annotation and reallocate the vector — a reference taken
    // before that call would dangle.
    std::wstring existing;
    PointD       anchor{};
    {
        Graphics measure(canvas_);
        const Annotation& mark = annotations_[index];
        existing = mark.text;
        // A Text mark is edited where it already is. Anything else opens
        // the field where its label will be drawn, so the glyphs do not
        // jump on commit — the rule the Text tool has always followed.
        const RectD box = mark.LabelBox(&measure);
        anchor = (mark.tool == Tool::Text) ? mark.start
                                           : PointD{ box.MinX(), box.MinY() };
    }

    BeginTextEntry(anchor);

    // Re-validated, because BeginTextEntry opens with CommitTextEntry and
    // that can ERASE an element — a Tool::Text mark committed blank is
    // deleted rather than left as an invisible ghost. The copies above
    // guard against the vector REALLOCATING; this guards against the index
    // no longer existing, which is a different failure and an out-of-bounds
    // write rather than a stale read.
    if (index >= static_cast<int>(annotations_.size())) {
        CancelTextEntry();
        return;
    }

    // Snapshot AFTER the field exists, not before. Snapshotting first meant
    // a failed CreateWindowEx — or an Esc — left a spent undo step that
    // visibly did nothing, and Snapshot had already cleared the redo stack.
    // Popping it back off was the previous attempt at this and is not safe:
    // if the push hit kUndoCap it erased the oldest entry, which a pop
    // cannot restore.
    if (!textEntryActive_) return;

    Snapshot();
    labelIndex_ = index;

    // Taken OFF the mark for the duration. Otherwise the committed label is
    // drawn on the canvas underneath a field showing the same words, and as
    // characters come and go the longer one shows through behind.
    labelBeingEdited_ = std::move(existing);
    annotations_[index].text.clear();

    ::SetWindowTextW(textEdit_, labelBeingEdited_.c_str());
    ::SendMessageW(textEdit_, EM_SETSEL, 0, -1);
    ::InvalidateRect(canvas_, nullptr, FALSE);
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

    // Taken and cleared before either early return below, or a mark whose
    // label was left empty would keep claiming the next label typed
    // anywhere else on the canvas.
    const int labelTarget = labelIndex_;
    labelIndex_ = -1;
    labelBeingEdited_.clear();   // committed, so the stashed copy is spent

    // Trim; a label of nothing but spaces is not a label.
    size_t first = value.find_first_not_of(L" \t\r\n");
    size_t last  = value.find_last_not_of(L" \t\r\n");
    // Empty is a legitimate value for a LABEL — it means "take the label
    // off again" — but for a standalone Text mark it means there was never
    // anything to commit.
    const bool blank = (first == std::wstring::npos);
    if (!blank) value = value.substr(first, last - first + 1);

    if (labelTarget >= 0) {
        // No second Snapshot: BeginLabelEntry already took one, and taking
        // another here would make Ctrl+Z remove the words and leave an
        // otherwise-untouched mark behind — two undo steps for one action.
        //
        // Range checked only. The tool is deliberately NOT checked — any
        // mark can carry a label now, which is the point — so there is no
        // identity test left to make. The window in which the array could
        // be reordered under an open field is narrow (the shortcut hook and
        // the accelerators both stand down while it has focus, and a
        // toolbar click commits through EN_KILLFOCUS first), but it is a
        // window, and this is a bounds check rather than a guarantee.
        if (labelTarget < static_cast<int>(annotations_.size())) {
            Annotation& mark = annotations_[labelTarget];
            if (blank && mark.tool == Tool::Text) {
                // A Text mark with no text is a ghost: it draws nothing but
                // still has a bounding box, so it stays selectable and
                // movable and rides along in every snapshot. Emptying one
                // means deleting it.
                annotations_.erase(annotations_.begin() + labelTarget);
                selectedIndex_ = -1;
            } else {
                mark.text = blank ? std::wstring() : std::move(value);
            }
        }
        RefreshToolbarState();
        ::InvalidateRect(canvas_, nullptr, FALSE);
        return;
    }

    if (blank) return;   // no snapshot, no annotation

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
    // A cancelled label leaves its mark alone. Esc says "not those
    // words", not "not that rectangle".
    const bool wasLabel = labelIndex_ >= 0;
    const int  labelTarget = labelIndex_;
    labelIndex_ = -1;
    // The words go back on the mark. Esc means "not those words", not
    // "delete the label I already had".
    if (wasLabel && labelTarget < static_cast<int>(annotations_.size())) {
        annotations_[labelTarget].text = std::move(labelBeingEdited_);
    }
    labelBeingEdited_.clear();
    ::SetFocus(canvas_);
    ::DestroyWindow(field);
    if (wasLabel) ::InvalidateRect(canvas_, nullptr, FALSE);
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

    // UPWARDS, above the swatch, which is where it has always opened.
    //
    // 1.8.1 briefly opened it downwards, out of the window, on the theory
    // that rising into the canvas was what made it cover the picture. It
    // was not: the picker was the right size and in the right place, and
    // the colour WHEEL inside it was drawn at twice its cell, spilling out
    // of the popup entirely. That is fixed where it is drawn. Opening
    // downwards fixed nothing and put the picker somewhere it did not
    // belong — a swatch on the bottom bar opens upwards, the way every
    // other bottom-anchored menu on Windows does.
    //
    // Clamped to the work area all the same, so a window dragged to the
    // top of the screen cannot put the picker off the top of the desk.
    int left = swatchRect.left;
    int top  = swatchRect.top - height - 4;

    HMONITOR monitor = ::MonitorFromRect(&swatchRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && ::GetMonitorInfoW(monitor, &info)) {
        if (top < info.rcWork.top) {
            top = swatchRect.bottom + 4;
            // And clamp the flip, or a work area too short for either
            // direction drops the picker off the bottom of the desk.
            top = (std::min)(top, static_cast<int>(info.rcWork.bottom) - height);
            top = (std::max)(top, static_cast<int>(info.rcWork.top));
        }
        left = (std::max)(static_cast<int>(info.rcWork.left),
                          (std::min)(left, static_cast<int>(info.rcWork.right) - width));
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
        HDC target = ::BeginPaint(hwnd, &paint);
        if (!target) { ::EndPaint(hwnd, &paint); return 0; }

        RECT client{};
        ::GetClientRect(hwnd, &client);

        // Buffered, like everything else that paints in passes. Ten cells,
        // a six-wedge wheel and eleven outlines drawn straight to the
        // screen is the flash you see when the picker opens.
        //
        // Scoped so the blit happens, and the buffer is destroyed, BEFORE
        // EndPaint releases the DC it blits into.
        {
        BufferedDC buffer(target, client);
        HDC dc = buffer.dc(target);
        // The buffer starts as uninitialised memory, so the background the
        // class used to erase for us has to be painted explicitly. Same
        // COLOR_WINDOW the class brush uses.
        ::FillRect(dc, &client, ::GetSysColorBrush(COLOR_WINDOW));

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
                // Half the cell, less a margin for the border drawn below.
                //
                // This was kCellSize — the WHOLE cell — with a comment
                // claiming it overshot "so wedges reach the corners". It
                // overshot by a factor of two, and nothing clipped it: Pie
                // takes a bounding box, not a cell, so the wheel was drawn
                // 16px past every edge of its square. That square is the
                // last column of the bottom row, so the overflow left the
                // popup itself and sat on the toolbar and the canvas.
                //
                // A circle inscribed in the cell is also the conventional
                // way to say "custom colour", so nothing is lost by it
                // staying inside.
                const int radius = kCellSize / 2 - 2;
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

    case WM_ERASEBKGND:
        // The buffered paint above covers every pixel, so an erase pass is
        // a full-window fill the user can see, immediately overdrawn.
        return 1;

    case WM_KEYDOWN:
        // The popup is an OWNED window, not a child — WS_POPUP with hwnd_
        // as its parent parameter — so IsChild is false for it and
        // PreTranslateMessage skips it entirely. It also takes the
        // foreground when it opens, so while it is up it holds focus and
        // Esc would otherwise reach DefWindowProc and be dropped.
        //
        // Deliberately NOT routed to the editor's close binding: an open
        // picker is the smallest outstanding thing, so Esc dismisses it
        // and a second Esc closes the window. Widening the hook to walk
        // the owner chain would have skipped that rung.
        if (wParam == VK_ESCAPE) {
            HideColourPopup();
            ReturnFocusToCanvas();
            return 0;
        }
        break;

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

    // Sized to the CROP. This is the whole of what cropping means for the
    // file you end up with: the capture behind it is untouched and still
    // full size, and what leaves the editor is the region you chose.
    auto output = Bitmap::Create(CropWidth(), CropHeight());
    if (!output || !output->MemoryDC() || !image_->MemoryDC()) return nullptr;

    ::BitBlt(output->MemoryDC(), 0, 0, CropWidth(), CropHeight(),
             image_->MemoryDC(), crop_.left, crop_.top, SRCCOPY);
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
        // Offset by the crop origin, not scaled: marks are in original
        // capture coordinates, and the output starts at the crop's
        // top-left. A mark outside the crop draws off the edge of the
        // bitmap and GDI+ discards it, which is exactly right — it still
        // exists, and undoing the crop brings it back.
        const PointD offset{ -static_cast<double>(crop_.left),
                             -static_cast<double>(crop_.top) };
        for (const Annotation& annotation : annotations_) {
            annotation.Draw(graphics, 1.0, offset, picture.get());
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

void EditorWindow::FlashButton(HWND button) {
    // A disabled button does not flash: see the note in the header. This is
    // also what makes it safe to flash BEFORE running the command rather
    // than after — the button's enabled state still describes whether the
    // command is about to do anything, which after the fact it may not.
    if (!button || !::IsWindowEnabled(button)) return;

    // Nor while a label is being typed. Ctrl+Z with the inline field open
    // backs out of the LABEL — Undo() cancels the text entry and returns
    // without touching the undo stack — so flashing Undo would claim an undo
    // that did not happen, which is the one thing this feature exists not to
    // do. Reachable only when focus has left the field without EN_KILLFOCUS
    // clearing the flag, which HandleEditorKey already guards against, so
    // this is narrow; IsWindowEnabled cannot see it either way.
    if (textEntryActive_) return;

    // A different button already lit: let it go now rather than leaving two
    // pressed until the one timer fires.
    if (flashingButton_ && flashingButton_ != button) ReleaseFlashedButton();

    flashingButton_ = button;
    ::SendMessageW(button, BM_SETSTATE, TRUE, 0);

    // Restarted, not stacked. Holding Ctrl+Z down auto-repeats, and each
    // repeat pushes the release out — so the button stays down for the whole
    // run of undos and comes up once, which is what the gesture looks like.
    ::KillTimer(hwnd_, kButtonFlashTimer);
    ::SetTimer(hwnd_, kButtonFlashTimer, kButtonFlashMs, nullptr);
}

void EditorWindow::ReleaseFlashedButton() {
    ::KillTimer(hwnd_, kButtonFlashTimer);

    // Taken and cleared BEFORE the send. BM_SETSTATE on an owner-drawn
    // button sends WM_DRAWITEM back to this window synchronously, so this
    // has to be re-entrant-safe — the same rule that WM_LBUTTONUP learned
    // the hard way with ReleaseCapture in 1.9.1.
    const HWND button = flashingButton_;
    flashingButton_ = nullptr;
    if (button) ::SendMessageW(button, BM_SETSTATE, FALSE, 0);
}
