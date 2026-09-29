# v1.8.4

The toolbar, finished properly.

## The slider thumb was not a styling quirk

It sat above its track because a horizontal comctl32 trackbar created without
`TBS_BOTH` gets a downward-**pointing** thumb — and Windows makes room for
the point by pushing the channel up off centre. One flag would have fixed the
position.

It would not have fixed the shape, which is the system's and is not
restylable, so the slider is now drawn by the editor: a rounded track, the
travelled part in the accent colour, and a round white thumb with an accent
ring.

It holds **no value of its own**. `currentLineWidth_` is the single copy and
the slider reads and writes it directly, so there is no `TBM_SETPOS` round
trip and no way for the control and the editor to hold different numbers. It
also responds to the mouse wheel, which the trackbar only did once focused.

## Rounded corners, and why they needed GDI+

GDI has `RoundRect` and it is unusable here: GDI does not antialias, so a GDI
rounded corner is a staircase — *worse* than the square corner it replaces at
34 × 28. GDI+ antialiases, was already linked, and is already initialised for
annotations, so this is a different drawing call rather than a new
dependency.

Two details that would otherwise look wrong:

- **The square is filled with `COLOR_BTNFACE` first.** Owner-draw hands over
  a DC with no promise about what is in it, and a rounded shape leaves its
  corners uncovered. Filling first is what makes the button read as sitting
  *on* a bar.
- **`PixelOffsetModeHalf`, and a half-pixel inset.** A GDI+ outline is
  centred on its path, so drawing on the exact bounds puts half the line
  outside the button where the neighbour paints over it — and without the
  pixel-offset mode a one-pixel border straddles the grid and comes out as
  two half-covered greys instead of one line.

## What it costs

**Nothing measurable at runtime.** The toolbar is not in a hot path.
`WM_DRAWITEM` fires only when a button is invalidated — ten call sites, every
one a user action. The thing that runs on every `WM_MOUSEMOVE` is
`PaintCanvas`, and none of this touches it. Button painting could get ten
times slower and it would be invisible.

**Negligible in size and memory.** A few hundred bytes of code; the pens and
brushes are per-paint and scoped, as they already were.

**One genuine cost.** Hand-drawn chrome stops following Windows: no dark
mode, no high-contrast mode, no accent-colour follow. The buttons gave that
up in 1.8.0 when they became owner-drawn. The slider was the last control the
system still themed, and it gives it up here. For a tool with a deliberately
light toolbar that is the right trade — but it is a trade, and it is now
made.

**And one regression worth naming.** The trackbar exposed itself to screen
readers through MSAA/UIA. The custom control implements no accessibility
interface at all. Nothing else in the program does either, so this is
consistent rather than newly broken — but it is a door closing, not a
non-issue.

## Verification

1. The slider thumb should sit centred on its track, and the track should be
   filled to the left of it.
2. Drag it; scroll the wheel over it; check the stroke width follows both.
3. With a mark selected, drag the slider — the mark should restyle live, and
   one drag should be one undo step rather than a hundred.
4. Every button should have soft rounded corners with no jagged staircase,
   and the corners should be the same grey as the bar rather than white.
5. The selected tool's ring should still be obvious across the room.
