#include "Annotation.h"

#include "Util.h"

// See the note in Bitmap.cpp: gdiplustypes.h needs min/max, and this project
// builds with NOMINMAX.
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <objidl.h>
#include <gdiplus.h>

using namespace Gdiplus;

namespace {

constexpr double kPi = 3.14159265358979323846;

Color ToGdipColour(COLORREF colour) {
    return Color(255, GetRValue(colour), GetGValue(colour), GetBValue(colour));
}

PointF Map(PointD point, double scale, PointD offset) {
    return PointF(static_cast<REAL>(point.x * scale + offset.x),
                  static_cast<REAL>(point.y * scale + offset.y));
}

RectD RectBetween(PointD a, PointD b) {
    RectD rect;
    rect.x      = (std::min)(a.x, b.x);
    rect.y      = (std::min)(a.y, b.y);
    rect.width  = std::fabs(b.x - a.x);
    rect.height = std::fabs(b.y - a.y);
    return rect;
}

RectD Inset(const RectD& rect, double dx, double dy) {
    RectD out;
    out.x      = rect.x + dx;
    out.y      = rect.y + dy;
    out.width  = rect.width - dx * 2;
    out.height = rect.height - dy * 2;
    return out;
}

bool Contains(const RectD& rect, PointD point) {
    // A rect that has been inset past its own size is empty and contains
    // nothing — which is what makes a thin rectangle read as solid to the
    // outline test rather than untouchable.
    if (rect.width <= 0 || rect.height <= 0) return false;
    return point.x >= rect.MinX() && point.x <= rect.MaxX()
        && point.y >= rect.MinY() && point.y <= rect.MaxY();
}

Font MakeFont(double pixelSize) {
    // Intentionally never destroyed. A function-local static of a GDI+ type
    // would run its destructor at CRT exit — after GdiplusShutdown, which is
    // a use-after-teardown. One family object for the process is a fair
    // trade for not having that.
    static const FontFamily* family = new FontFamily(L"Segoe UI");
    return Font(family, static_cast<REAL>(pixelSize), FontStyleBold, UnitPixel);
}

RectF MeasureText(Graphics* graphics, const std::wstring& text, double fontSize) {
    if (!graphics || text.empty()) return RectF(0, 0, 0, 0);
    Font font = MakeFont(fontSize);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(format.GetFormatFlags() | StringFormatFlagsNoWrap);
    RectF bounds;
    graphics->MeasureString(text.c_str(), static_cast<INT>(text.size()), &font,
                            PointF(0, 0), &format, &bounds);
    return bounds;
}

} // namespace

// ---------------------------------------------------------------------------

const wchar_t* ToolKeyValue(Tool tool) {
    switch (tool) {
    case Tool::Rectangle: return L"rectangle";
    case Tool::Ellipse:   return L"ellipse";
    case Tool::Line:      return L"line";
    case Tool::Pen:       return L"pen";
    case Tool::Text:      return L"text";
    default:              return L"arrow";
    }
}

const wchar_t* ToolTitle(Tool tool) {
    switch (tool) {
    case Tool::Rectangle: return L"Rectangle";
    case Tool::Ellipse:   return L"Ellipse";
    case Tool::Line:      return L"Line";
    case Tool::Pen:       return L"Pen";
    case Tool::Text:      return L"Text";
    default:              return L"Arrow";
    }
}

Tool ToolFromKeyValue(const std::wstring& value) {
    if (value == L"rectangle") return Tool::Rectangle;
    if (value == L"ellipse")   return Tool::Ellipse;
    if (value == L"line")      return Tool::Line;
    if (value == L"pen")       return Tool::Pen;
    if (value == L"text")      return Tool::Text;
    return Tool::Arrow;
}

// ---------------------------------------------------------------------------

RectD Annotation::NormalizedRect() const {
    if (tool == Tool::Pen) {
        if (points.empty()) return RectD{};
        double minX = points.front().x, maxX = points.front().x;
        double minY = points.front().y, maxY = points.front().y;
        for (const PointD& point : points) {
            minX = (std::min)(minX, point.x);
            maxX = (std::max)(maxX, point.x);
            minY = (std::min)(minY, point.y);
            maxY = (std::max)(maxY, point.y);
        }
        return RectD{ minX, minY, maxX - minX, maxY - minY };
    }
    return RectBetween(start, end);
}

RectD Annotation::BoundingBox(Graphics* measureWith) const {
    if (tool == Tool::Text) {
        const RectF measured = MeasureText(measureWith, text, FontSize());
        return RectD{ start.x, start.y,
                      measured.Width + 8.0 + kTextInset,
                      TextBoxHeight(FontSize()) };
    }
    return NormalizedRect();
}

// --- drawing ---------------------------------------------------------------

void Annotation::Draw(Graphics& graphics, double scale, PointD offset) const {
    const Color colourValue = ToGdipColour(colour);
    const REAL  width = static_cast<REAL>((std::max)(1.0, lineWidth * scale));

    Pen pen(colourValue, width);
    SolidBrush brush(colourValue);

    switch (tool) {
    case Tool::Rectangle: {
        pen.SetLineJoin(LineJoinRound);
        const RectD rect = RectBetween(start, end);
        const PointF a = Map({ rect.MinX(), rect.MinY() }, scale, offset);
        const PointF b = Map({ rect.MaxX(), rect.MaxY() }, scale, offset);
        graphics.DrawRectangle(&pen, a.X, a.Y, b.X - a.X, b.Y - a.Y);
        break;
    }
    case Tool::Ellipse: {
        const RectD rect = RectBetween(start, end);
        const PointF a = Map({ rect.MinX(), rect.MinY() }, scale, offset);
        const PointF b = Map({ rect.MaxX(), rect.MaxY() }, scale, offset);
        graphics.DrawEllipse(&pen, a.X, a.Y, b.X - a.X, b.Y - a.Y);
        break;
    }
    case Tool::Line: {
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        graphics.DrawLine(&pen, Map(start, scale, offset), Map(end, scale, offset));
        break;
    }
    case Tool::Arrow: {
        const PointF from = Map(start, scale, offset);
        const PointF to   = Map(end, scale, offset);
        const double dx = to.X - from.X;
        const double dy = to.Y - from.Y;
        const double length = std::hypot(dx, dy);
        if (length <= 0.5) break;   // a click, not a drag: draw nothing at all

        const double angle = std::atan2(dy, dx);
        // The 12-pixel floor is applied in IMAGE units and only then scaled.
        // Computing it in device units would make the head fatter on screen
        // than in the exported file.
        const double headLength = (std::min)((std::max)(lineWidth * 4.0, 12.0) * scale, length);
        const double spread = kPi / 7.0;   // half-angle; 51.4 degrees included

        // The shaft stops short of the tip so the point stays sharp.
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        const PointF shaftEnd(static_cast<REAL>(to.X - std::cos(angle) * headLength * 0.8),
                              static_cast<REAL>(to.Y - std::sin(angle) * headLength * 0.8));
        graphics.DrawLine(&pen, from, shaftEnd);

        PointF head[3] = {
            to,
            PointF(static_cast<REAL>(to.X - headLength * std::cos(angle - spread)),
                   static_cast<REAL>(to.Y - headLength * std::sin(angle - spread))),
            PointF(static_cast<REAL>(to.X - headLength * std::cos(angle + spread)),
                   static_cast<REAL>(to.Y - headLength * std::sin(angle + spread)))
        };
        graphics.FillPolygon(&brush, head, 3);
        break;
    }
    case Tool::Pen: {
        if (points.size() <= 1) break;
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        std::vector<PointF> mapped;
        mapped.reserve(points.size());
        for (const PointD& point : points) mapped.push_back(Map(point, scale, offset));
        graphics.DrawLines(&pen, mapped.data(), static_cast<INT>(mapped.size()));
        break;
    }
    case Tool::Text: {
        if (text.empty()) break;
        Font font = MakeFont(FontSize() * scale);
        StringFormat format(StringFormat::GenericTypographic());
        format.SetFormatFlags(format.GetFormatFlags() | StringFormatFlagsNoWrap);
        format.SetLineAlignment(StringAlignmentNear);
        format.SetAlignment(StringAlignmentNear);

        const PointF origin = Map(start, scale, offset);
        // Laid out from the box's top edge, exactly as the inline edit
        // control does while typing, so the glyphs do not jump on commit.
        RectF box(origin.X + static_cast<REAL>(kTextInset * scale),
                  origin.Y,
                  static_cast<REAL>(4096.0),
                  static_cast<REAL>(TextBoxHeight(FontSize()) * scale));
        graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, box, &format, &brush);
        break;
    }
    }
}

// --- hit-testing -----------------------------------------------------------

bool Annotation::HitTest(PointD point, double tolerance, Graphics* measureWith) const {
    switch (tool) {
    case Tool::Rectangle: {
        // Closed shapes are hit on their OUTLINE, never their interior:
        // clicking inside an empty rectangle starts a new drawing, which is
        // nearly always what you meant.
        const RectD rect = NormalizedRect();
        if (!Contains(Inset(rect, -tolerance, -tolerance), point)) return false;
        return !Contains(Inset(rect, tolerance, tolerance), point);
    }
    case Tool::Ellipse: {
        const RectD rect = NormalizedRect();
        const double rx = (std::max)(rect.width / 2, 0.001);
        const double ry = (std::max)(rect.height / 2, 0.001);
        const double dx = (point.x - rect.MidX()) / rx;
        const double dy = (point.y - rect.MidY()) / ry;
        const double band = (std::max)(tolerance / (std::min)(rx, ry) * 2, 0.15);
        return std::fabs(dx * dx + dy * dy - 1.0) < band;
    }
    case Tool::Line:
    case Tool::Arrow:
        // The head is not separately tested; grabbing the shaft is enough.
        return util::PointSegmentDistance(point.x, point.y, start.x, start.y, end.x, end.y)
               <= tolerance;
    case Tool::Pen: {
        if (points.size() <= 1) return false;
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            if (util::PointSegmentDistance(point.x, point.y,
                                           points[i].x, points[i].y,
                                           points[i + 1].x, points[i + 1].y) <= tolerance) {
                return true;
            }
        }
        return false;
    }
    case Tool::Text:
        // Text is the one exception: its whole box counts, because a label
        // has no meaningful outline to aim at.
        return Contains(Inset(BoundingBox(measureWith), -tolerance, -tolerance), point);
    }
    return false;
}

std::vector<std::pair<Handle, PointD>> Annotation::Handles() const {
    std::vector<std::pair<Handle, PointD>> out;
    switch (tool) {
    case Tool::Line:
    case Tool::Arrow:
        // The raw endpoints, deliberately not normalised: an arrow has a
        // direction and its two ends must stay distinguishable.
        out.push_back({ Handle::Start, start });
        out.push_back({ Handle::End, end });
        break;
    case Tool::Rectangle:
    case Tool::Ellipse: {
        const RectD r = NormalizedRect();
        out.push_back({ Handle::TopLeft,     { r.MinX(), r.MinY() } });
        out.push_back({ Handle::Top,         { r.MidX(), r.MinY() } });
        out.push_back({ Handle::TopRight,    { r.MaxX(), r.MinY() } });
        out.push_back({ Handle::Right,       { r.MaxX(), r.MidY() } });
        out.push_back({ Handle::BottomRight, { r.MaxX(), r.MaxY() } });
        out.push_back({ Handle::Bottom,      { r.MidX(), r.MaxY() } });
        out.push_back({ Handle::BottomLeft,  { r.MinX(), r.MaxY() } });
        out.push_back({ Handle::Left,        { r.MinX(), r.MidY() } });
        break;
    }
    default:
        // Pen strokes and text move but do not resize. Text size follows the
        // stroke-width slider; rescaling a freehand stroke isn't supported.
        break;
    }
    return out;
}

void Annotation::MoveBy(double dx, double dy) {
    start.x += dx; start.y += dy;
    end.x   += dx; end.y   += dy;
    for (PointD& point : points) { point.x += dx; point.y += dy; }
}

void Annotation::Resize(Handle handle, const RectD& original, PointD to) {
    if (handle == Handle::Start) { start = to; return; }
    if (handle == Handle::End)   { end = to; return; }

    double minX = original.MinX(), maxX = original.MaxX();
    double minY = original.MinY(), maxY = original.MaxY();

    switch (handle) {
    case Handle::TopLeft:     minX = to.x; minY = to.y; break;
    case Handle::Top:                      minY = to.y; break;
    case Handle::TopRight:    maxX = to.x; minY = to.y; break;
    case Handle::Right:       maxX = to.x;              break;
    case Handle::BottomRight: maxX = to.x; maxY = to.y; break;
    case Handle::Bottom:                   maxY = to.y; break;
    case Handle::BottomLeft:  minX = to.x; maxY = to.y; break;
    case Handle::Left:        minX = to.x;              break;
    default: return;
    }

    // Re-normalise, so dragging a handle past the opposite edge flips the
    // shape rather than producing a negative size.
    start = { (std::min)(minX, maxX), (std::min)(minY, maxY) };
    end   = { (std::max)(minX, maxX), (std::max)(minY, maxY) };
}
