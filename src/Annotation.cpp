#include "Annotation.h"

#include "Util.h"

// See the note in Bitmap.cpp: gdiplustypes.h needs min/max, and this project
// builds with NOMINMAX.
#include <algorithm>
#include <cmath>
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
    case Tool::Lift:      return L"lift";
    case Tool::Crop:      return L"crop";
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
    case Tool::Lift:      return L"Lift";
    case Tool::Crop:      return L"Crop";
    default:              return L"Arrow";
    }
}

Tool ToolFromKeyValue(const std::wstring& value) {
    if (value == L"rectangle") return Tool::Rectangle;
    if (value == L"ellipse")   return Tool::Ellipse;
    if (value == L"line")      return Tool::Line;
    if (value == L"pen")       return Tool::Pen;
    if (value == L"text")      return Tool::Text;
    if (value == L"lift")      return Tool::Lift;
    // Arrow needs its own branch, and the lack of one was a real bug for
    // exactly one release.
    //
    // ToolKeyValue writes Arrow through its `default:` arm, so "arrow" is a
    // string this function genuinely has to parse. It had no branch for it
    // and got away with that only because the fallback below USED to be
    // Arrow — the missing case and the fallback happened to agree. 1.9.6
    // moved the default to Rectangle and they stopped agreeing, so
    // selecting Arrow, closing the editor and opening another silently gave
    // you Rectangle. Worse, editor_settings::IsDefault then compared
    // Rectangle against Rectangle and reported "at defaults" while a stale
    // "arrow" sat in the registry, which greyed out the one menu item that
    // would have cleared it.
    if (value == L"arrow")     return Tool::Arrow;
    // Deliberately absent: "crop" never round-trips. It is an action, not a
    // mode, so the editor disarms it after one and there is nothing
    // sensible to restore a session into.
    //
    // Also the fallback for a value that is missing, misspelt or from a
    // newer version that had a tool this one does not.
    return kDefaultTool;
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

PointD Annotation::LabelDirection() const {
    // Wrapped rather than clamped: the index is modular by nature, and a
    // clamp would make dragging past west stick instead of coming round.
    const int index = ((labelAngle % kLabelDirections) + kLabelDirections)
                      % kLabelDirections;
    const double radians = index * (2.0 * kPi / kLabelDirections);
    return PointD{ std::cos(radians), std::sin(radians) };
}

namespace {
// Half the extent of a w x h box along `dir` — its support function. Used to
// push the label out far enough that `gap` is the visible space between the
// mark and the label's NEAR edge, whichever direction it sits in. Without
// it a diagonal label would crowd the mark while a horizontal one would not.
double HalfExtentAlong(PointD dir, double w, double h) {
    return (std::fabs(dir.x) * w + std::fabs(dir.y) * h) / 2.0;
}

// Where a ray leaves an axis-aligned box from its centre.
// Named EdgePoint, not ExitPoint, and the local is `edge` not `exit`:
// <cstdlib> declares a global ::exit, and hiding it is C4459 — which
// /WX turns into a build failure.
PointD EdgePoint(const RectD& box, PointD dir) {
    const double halfW = (std::max)(box.width, 0.0) / 2.0;
    const double halfH = (std::max)(box.height, 0.0) / 2.0;
    const double big   = 1.0e9;
    const double tx = (std::fabs(dir.x) < 1.0e-9) ? big : halfW / std::fabs(dir.x);
    const double ty = (std::fabs(dir.y) < 1.0e-9) ? big : halfH / std::fabs(dir.y);
    const double t  = (std::min)(tx, ty);
    return PointD{ box.MidX() + dir.x * t, box.MidY() + dir.y * t };
}
} // namespace

RectD Annotation::LabelBox(Graphics* measureWith) const {
    const double height = TextBoxHeight(FontSize());
    const double width  = MeasureText(measureWith, text, FontSize()).Width
                          + 8.0 + kTextInset;

    const PointD dir  = LabelDirection();
    const RectD  bounds = NormalizedRect();
    const PointD edge = EdgePoint(bounds, dir);
    const double push = (std::max)(kMinLabelGap, labelGap)
                        + HalfExtentAlong(dir, width, height);

    const double cx = edge.x + dir.x * push;
    const double cy = edge.y + dir.y * push;
    return RectD{ cx - width / 2.0, cy - height / 2.0, width, height };
}

void Annotation::LabelLeader(Graphics* measureWith, PointD* from, PointD* to) const {
    const PointD dir  = LabelDirection();
    const RectD  box  = LabelBox(measureWith);
    const PointD edge = EdgePoint(NormalizedRect(), dir);

    // From the mark's edge to the label's near edge, so the leader is
    // exactly the gap and never disappears under either end.
    const double half = HalfExtentAlong(dir, box.width, box.height);
    if (from) *from = edge;
    if (to)   *to   = PointD{ box.MidX() - dir.x * half, box.MidY() - dir.y * half };
}

void Annotation::AimLabelAt(PointD target, Graphics* measureWith) {
    const RectD bounds = NormalizedRect();
    const double dx = target.x - bounds.MidX();
    const double dy = target.y - bounds.MidY();
    if (std::hypot(dx, dy) < 1.0) return;   // no direction to read

    const double step = 2.0 * kPi / kLabelDirections;
    int index = static_cast<int>(std::lround(std::atan2(dy, dx) / step));
    index = ((index % kLabelDirections) + kLabelDirections) % kLabelDirections;
    labelAngle = index;

    // The gap is measured along the SNAPPED ray, not to the cursor, so the
    // label does not lurch outwards as the angle clicks over to the next
    // one. Measured to the label's near edge, which is what the user sees.
    const PointD dir  = LabelDirection();
    const PointD edge = EdgePoint(bounds, dir);
    const double along = (target.x - edge.x) * dir.x + (target.y - edge.y) * dir.y;

    const double height = TextBoxHeight(FontSize());
    const double width  = MeasureText(measureWith, text, FontSize()).Width
                          + 8.0 + kTextInset;
    labelGap = (std::max)(kMinLabelGap, along - HalfExtentAlong(dir, width, height));
}

RectD Annotation::BoundingBox(Graphics* measureWith) const {
    if (tool == Tool::Text) {
        const RectF measured = MeasureText(measureWith, text, FontSize());
        return RectD{ start.x, start.y,
                      measured.Width + 8.0 + kTextInset,
                      TextBoxHeight(FontSize()) };
    }
    const RectD shape = NormalizedRect();
    if (!HasLabel()) return shape;

    // The mark AND its label, for every tool that can carry one. Selection
    // chrome and hit-testing both key off this, so leaving the label out
    // would mean only half the object could be found or drawn around.
    const RectD label = LabelBox(measureWith);
    const double minX = (std::min)(shape.MinX(), label.MinX());
    const double minY = (std::min)(shape.MinY(), label.MinY());
    const double maxX = (std::max)(shape.MaxX(), label.MaxX());
    const double maxY = (std::max)(shape.MaxY(), label.MaxY());
    return RectD{ minX, minY, maxX - minX, maxY - minY };
}

// --- drawing ---------------------------------------------------------------

void Annotation::Draw(Graphics& graphics, double scale, PointD offset,
                      Gdiplus::Image* picture) const {
    const Color colourValue = ToGdipColour(colour);
    const REAL  width = static_cast<REAL>((std::max)(1.0, lineWidth * scale));

    Pen pen(colourValue, width);
    SolidBrush brush(colourValue);

    switch (tool) {
    case Tool::Lift: {
        // Still being dragged out: there is no source rectangle yet, because
        // the region is only fixed on mouse-up. Draw a marquee so the drag is
        // visible — without this the tool looks broken while it is being used,
        // which is the only moment it matters.
        if (source.width <= 0.0 || source.height <= 0.0) {
            Pen marquee(ToGdipColour(colour), 1.0f);
            marquee.SetDashStyle(DashStyleDash);
            const RectD box = RectBetween(start, end);
            const PointF p0 = Map({ box.MinX(), box.MinY() }, scale, offset);
            const PointF p1 = Map({ box.MaxX(), box.MaxY() }, scale, offset);
            graphics.DrawRectangle(&marquee, p0.X, p0.Y, p1.X - p0.X, p1.Y - p0.Y);
            break;
        }
        if (!picture) break;   // nothing to read from; see the header

        // The blank goes down first, so that dragging a lifted piece back over
        // its own source covers the patch rather than being covered by it.
        if (blankSource) {
            SolidBrush fill(ToGdipColour(blankColour));
            const PointF a = Map({ source.MinX(), source.MinY() }, scale, offset);
            const PointF b = Map({ source.MaxX(), source.MaxY() }, scale, offset);
            graphics.FillRectangle(&fill, a.X, a.Y, b.X - a.X, b.Y - a.Y);
        }

        const RectD rect = RectBetween(start, end);
        const PointF a = Map({ rect.MinX(), rect.MinY() }, scale, offset);
        const PointF b = Map({ rect.MaxX(), rect.MaxY() }, scale, offset);
        const RectF destination(a.X, a.Y, b.X - a.X, b.Y - a.Y);

        // NearestNeighbor rather than the default interpolation. At scale 1 —
        // which is every export, and the common case on screen — it is an
        // exact copy of the pixels, where a smoothing filter would resample
        // text into mush. A lifted screenshot region is nearly always text or
        // UI, and both want their edges kept.
        const InterpolationMode previous = graphics.GetInterpolationMode();
        graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
        graphics.DrawImage(picture, destination,
                           static_cast<REAL>(source.x),     static_cast<REAL>(source.y),
                           static_cast<REAL>(source.width), static_cast<REAL>(source.height),
                           UnitPixel);
        graphics.SetInterpolationMode(previous);
        break;
    }
    case Tool::Rectangle: {
        const RectD rect = RectBetween(start, end);
        const PointF a = Map({ rect.MinX(), rect.MinY() }, scale, offset);
        const PointF b = Map({ rect.MaxX(), rect.MaxY() }, scale, offset);
        if (filled) {
            // Shift-drag. No outline as well: an outline in the same colour
            // is invisible, and in any other colour it is a second decision
            // the gesture never made.
            graphics.FillRectangle(&brush, a.X, a.Y, b.X - a.X, b.Y - a.Y);
        } else {
            pen.SetLineJoin(LineJoinRound);
            graphics.DrawRectangle(&pen, a.X, a.Y, b.X - a.X, b.Y - a.Y);
        }
        break;
    }
    case Tool::Ellipse: {
        const RectD rect = RectBetween(start, end);
        const PointF a = Map({ rect.MinX(), rect.MinY() }, scale, offset);
        const PointF b = Map({ rect.MaxX(), rect.MaxY() }, scale, offset);
        if (filled) graphics.FillEllipse(&brush, a.X, a.Y, b.X - a.X, b.Y - a.Y);
        else        graphics.DrawEllipse(&pen,   a.X, a.Y, b.X - a.X, b.Y - a.Y);
        break;
    }
    case Tool::Line: {
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        graphics.DrawLine(&pen, Map(start, scale, offset), Map(end, scale, offset));
        break;
    }
    case Tool::Crop: {
        // The only thing Crop ever draws. There is no committed Crop
        // annotation — EditorWindow intercepts the mouse-up and changes the
        // view instead — so this is strictly the in-progress marquee, and
        // it is dashed for the same reason Lift's is: it marks a region
        // rather than adding ink.
        Pen marquee(ToGdipColour(colour), 1.0f);
        marquee.SetDashStyle(DashStyleDash);
        const RectD box = RectBetween(start, end);
        const PointF p0 = Map({ box.MinX(), box.MinY() }, scale, offset);
        const PointF p1 = Map({ box.MaxX(), box.MaxY() }, scale, offset);
        graphics.DrawRectangle(&marquee, p0.X, p0.Y, p1.X - p0.X, p1.Y - p0.Y);
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

    // --- the label, for any mark that has one -------------------------------
    //
    // AFTER the switch, deliberately: this is one pass shared by every tool
    // rather than a clause bolted onto each of them, which is what makes a
    // labelled ellipse and a labelled arrow behave identically without
    // either of them knowing about labels.
    if (!HasLabel()) return;

    PointD leaderFrom{}, leaderTo{};
    // Measured against THIS Graphics, not the caller's, so the box the
    // glyphs are laid into is the box they were measured for.
    LabelLeader(&graphics, &leaderFrom, &leaderTo);

    // The leader is drawn at a thinner weight than the mark. A leader as
    // heavy as the rectangle it points at competes with it, and the label
    // is supposed to be an aside.
    Pen leader(colourValue, (std::max)(1.0f, width * 0.6f));
    leader.SetStartCap(LineCapRound);
    leader.SetEndCap(LineCapRound);
    graphics.DrawLine(&leader, Map(leaderFrom, scale, offset), Map(leaderTo, scale, offset));

    const RectD box = LabelBox(&graphics);
    Font labelFont = MakeFont(FontSize() * scale);
    StringFormat labelFormat(StringFormat::GenericTypographic());
    labelFormat.SetFormatFlags(labelFormat.GetFormatFlags() | StringFormatFlagsNoWrap);
    labelFormat.SetLineAlignment(StringAlignmentNear);
    labelFormat.SetAlignment(StringAlignmentNear);

    const PointF labelOrigin = Map({ box.MinX(), box.MinY() }, scale, offset);
    RectF labelTarget(labelOrigin.X + static_cast<REAL>(kTextInset * scale),
                      labelOrigin.Y,
                      static_cast<REAL>(box.width * scale),
                      static_cast<REAL>(box.height * scale));
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()),
                        &labelFont, labelTarget, &labelFormat, &brush);
}

// --- hit-testing -----------------------------------------------------------

bool Annotation::HitTest(PointD point, double tolerance, Graphics* measureWith) const {
    // The label first, and for every tool. Aiming at the text is the
    // natural way to grab a labelled mark — it is the big target and the
    // part you read — and requiring the shape itself would make the label
    // look inert.
    if (HasLabel() &&
        Contains(Inset(LabelBox(measureWith), -tolerance, -tolerance), point)) {
        return true;
    }

    switch (tool) {
    case Tool::Rectangle: {
        // An OUTLINE is hit on its outline, never its interior: clicking
        // inside an empty rectangle starts a new drawing, which is nearly
        // always what you meant. A FILLED one is solid, for the same reason
        // Lift is — there is no empty middle to click through to, and its
        // middle is the only part of it there is.
        const RectD rect = NormalizedRect();
        if (!Contains(Inset(rect, -tolerance, -tolerance), point)) return false;
        if (filled) return true;
        return !Contains(Inset(rect, tolerance, tolerance), point);
    }
    case Tool::Ellipse: {
        const RectD rect = NormalizedRect();
        const double rx = (std::max)(rect.width / 2, 0.001);
        const double ry = (std::max)(rect.height / 2, 0.001);
        const double dx = (point.x - rect.MidX()) / rx;
        const double dy = (point.y - rect.MidY()) / ry;
        const double distance = dx * dx + dy * dy;
        if (filled) {
            const double band = (std::max)(tolerance / (std::min)(rx, ry) * 2, 0.15);
            return distance <= 1.0 + band;
        }
        const double band = (std::max)(tolerance / (std::min)(rx, ry) * 2, 0.15);
        return std::fabs(distance - 1.0) < band;
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
        // An empty one is unhittable, belt to the editor's braces: the
        // editor deletes a Text mark whose text is cleared, and this makes
        // sure that even if one survived it could not be grabbed out of
        // thin air by its 10px empty box.
        if (text.empty()) return false;
        // Otherwise its whole box counts, because text has no meaningful
        // outline to aim at.
        return Contains(Inset(BoundingBox(measureWith), -tolerance, -tolerance), point);
    case Tool::Crop:
        // Unreachable: a Crop annotation is never committed, so there is
        // never one in the array to test. Present because the switch has no
        // default and /W4 turns a missing enumerator into a build failure —
        // which is the reminder you want when a tool is added.
        return false;
    case Tool::Lift:
        // Solid, so aiming at the outline would be aiming at an edge that
        // carries no meaning.
        return Contains(Inset(NormalizedRect(), -tolerance, -tolerance), point);
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
    case Tool::Ellipse:
    // A lifted piece resizes from its destination rectangle, like any other
    // box. The source rectangle is fixed once the lift is made: changing where
    // the pixels came from after the fact is a different operation, and not
    // one you can express by dragging a corner of the thing you are looking at.
    case Tool::Lift: {
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
