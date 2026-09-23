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

namespace Gdiplus { class Graphics; }

enum class Tool { Arrow, Rectangle, Ellipse, Line, Pen, Text };

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

    // The stroke-width slider doubles as the text-size control, with a floor
    // so a hairline stroke still produces a readable label.
    double FontSize() const { return (std::max)(14.0, lineWidth * 5.0); }
    static double TextBoxHeight(double fontSize) { return std::ceil(fontSize * 1.6); }
    static constexpr double kTextInset = 2.0;

    RectD NormalizedRect() const;
    RectD BoundingBox(Gdiplus::Graphics* measureWith) const;

    // `scale` maps image pixels to device units and is 1.0 when exporting;
    // `offset` is where the image's top-left corner sits in the target.
    void Draw(Gdiplus::Graphics& graphics, double scale, PointD offset) const;

    bool HitTest(PointD point, double tolerance, Gdiplus::Graphics* measureWith) const;

    std::vector<std::pair<Handle, PointD>> Handles() const;

    void MoveBy(double dx, double dy);
    // Always computed from the rectangle as it was when the drag began, never
    // from the live one — see ARCHITECTURE.md for the sliver bug that causes.
    void Resize(Handle handle, const RectD& original, PointD to);
};
