// Annotation.h — the shape model, its drawing, and its hit-testing.
//
// Annotations are stored in IMAGE PIXEL COORDINATES, never in view
// coordinates. The same drawing code runs for the on-screen canvas (scaled to
// whatever the window is) and for the export (scale 1), so the saved PNG is
// full resolution and matches exactly what was drawn, regardless of how big
// the editor window happened to be.
//
// Unlike the macOS original this uses a top-left origin throughout, matching
// GDI+ and every Windows coordinate the program handles. The shapes are the
// same; only "up" changed.

#pragma once

#include "framework.h"

// Image as well as Graphics now, because Lift's Draw takes the picture it
// reads its pixels out of. Forward declarations rather than gdiplus.h: this
// header is included widely, and Gdiplus::Bitmap collides with our ::Bitmap.
namespace Gdiplus { class Graphics; class Image; }

// Lift is the odd one out: every other tool draws ink of its own, while Lift
// re-draws a rectangle of the underlying picture somewhere else. It is still
// an Annotation rather than a change to the pixels, which is the whole point —
// it stays movable, resizable and undoable like any other mark, and the
// original capture is never modified.
//
// Callout is an Arrow that carries a label at its TAIL. It is one annotation
// rather than an arrow plus a separate Text mark so that moving it moves both
// halves, and so the label cannot be left behind pointing at nothing.
enum class Tool { Arrow, Rectangle, Ellipse, Line, Pen, Text, Lift, Callout };

// The toolbar builds its buttons, maps their command IDs back to tools, and
// sizes its button array from this. It was a literal 6 in four separate
// places, which is three chances to add a tool and update only some of them —
// and the failure is quiet: the button simply never appears, or appears and
// selects the wrong tool.
inline constexpr int kToolCount = 8;

// Which tools do something different when Shift is held. Lift cuts instead
// of copying; Rectangle and Ellipse fill instead of outlining. The canvas
// shows a hint while one of these is selected, and nothing at all while the
// others are, so the hint line always describes the tool in hand.
inline constexpr bool ToolHasShiftVariant(Tool tool) {
    return tool == Tool::Lift || tool == Tool::Rectangle || tool == Tool::Ellipse;
}

const wchar_t* ToolKeyValue(Tool tool);     // the persisted string
const wchar_t* ToolTitle(Tool tool);
Tool           ToolFromKeyValue(const std::wstring& value);

struct PointD { double x = 0.0; double y = 0.0; };
struct RectD  { double x = 0.0; double y = 0.0; double width = 0.0; double height = 0.0;
                double MinX() const { return x; }
                double MinY() const { return y; }
                double MaxX() const { return x + width; }
                double MaxY() const { return y + height; }
                double MidX() const { return x + width / 2; }
                double MidY() const { return y + height / 2; } };

// Eight for rectangles and ellipses, two for lines and arrows, none for pen
// strokes and text. Declaration order is hit-test order.
enum class Handle {
    None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Start, End
};

struct Annotation {
    Tool                tool      = Tool::Arrow;
    COLORREF            colour    = RGB(52, 199, 89);
    double              lineWidth = 4.0;      // image pixels, not points
    PointD              start;
    PointD              end;
    std::vector<PointD> points;               // freehand only
    std::wstring        text;                 // text tool only

    // Lift only. `source` is the rectangle the pixels are read from; start/end
    // are where they are drawn, so dragging and resizing the mark work on the
    // destination exactly as they do for a rectangle. `blankSource` fills the
    // source rectangle with `blankColour` first, turning the copy into a move.
    RectD    source;
    bool     blankSource = false;
    COLORREF blankColour = RGB(255, 255, 255);

    // The stroke-width slider doubles as the text-size control, with a floor
    // so a hairline stroke still produces a readable label.
    double FontSize() const { return (std::max)(14.0, lineWidth * 5.0); }
    static double TextBoxHeight(double fontSize) { return std::ceil(fontSize * 1.6); }
    static constexpr double kTextInset = 2.0;

    // Rectangle and Ellipse only: Shift-drag fills the shape with `colour`
    // instead of outlining it. This is what used to be a separate Redact
    // tool, and folding it in is the better shape — a filled rectangle IS a
    // redaction, and a tool whose only difference from Rectangle is the
    // brush did not earn a slot on the bar.
    //
    // The security argument the Redact tool carried still applies and is
    // worth keeping where someone will read it: a flat fill is the ONLY
    // safe way to cover text. Pixelation and blur both look like protection
    // while leaving the original recoverable — a screenshot has a known
    // font at a known size, so the attack is to render candidate text,
    // pixelate it on the same grid and compare. Forwards, not backwards,
    // one glyph at a time. A flat fill's output does not depend on the
    // pixels underneath, so there is nothing to work back from.
    bool filled = false;

    // Callout only: where the label sits relative to the arrow.
    //
    // At the TAIL, not the head. The head is on the thing you are pointing
    // at, so a label there covers the very pixels the arrow was drawn to
    // single out. At the tail the text sits in whatever empty space you
    // dragged out of, which is where you would have written it by hand.
    //
    // Needs a Graphics to measure the string when the arrow travels
    // rightwards, because the box then has to be placed by its right edge.
    // Null is legal and gives the unmeasured position: somewhere sensible
    // rather than nothing.
    static constexpr double kCalloutGap = 6.0;
    RectD CalloutLabelBox(Gdiplus::Graphics* measureWith) const;

    RectD NormalizedRect() const;
    RectD BoundingBox(Gdiplus::Graphics* measureWith) const;

    // `scale` maps image pixels to device units and is 1.0 when exporting;
    // `offset` is where the image's top-left corner sits in the target.
    //
    // `picture` is the unmodified capture, needed only by Lift, which reads
    // its pixels back out of it. Passing null is legal and makes a Lift mark
    // draw nothing rather than crash — the canvas and the export both have the
    // image to hand, but a future caller might not.
    void Draw(Gdiplus::Graphics& graphics, double scale, PointD offset,
              Gdiplus::Image* picture = nullptr) const;

    bool HitTest(PointD point, double tolerance, Gdiplus::Graphics* measureWith) const;

    std::vector<std::pair<Handle, PointD>> Handles() const;

    void MoveBy(double dx, double dy);
    // Always computed from the rectangle as it was when the drag began, never
    // from the live one — see ARCHITECTURE.md for the sliver bug that causes.
    void Resize(Handle handle, const RectD& original, PointD to);
};
