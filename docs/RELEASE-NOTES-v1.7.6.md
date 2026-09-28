# v1.7.6

One fix: the selection border no longer tears while you drag a region.

## What was happening

Dragging out a region left the border with a horizontal jog in it, near the
pointer. It looked like jagged drawing along the Y axis. It was not — the line
was drawn perfectly straight. You were seeing **half of one frame and half of
another**.

The region overlay is a full-desktop, topmost, opaque window, and it repaints
on every `WM_MOUSEMOVE`. That means a blit straight into the window's surface
at whatever rate the mouse reports — 125 times a second on an ordinary mouse,
several hundred on a gaming one. DWM copies that surface when it composes, on
its own clock, and nothing made the two take turns.

Land a blit while the compositor is reading and the frame it presents is part
new selection, part old, split along a single scanline:

```
        │                    the border, one frame
        │  ← top half: new position
        │
     ───┘                    ← the split
     │
     │     ← bottom half: still the previous position
     │
```

It shows up beside the pointer because the pointer is where the only changing
pixels are. Everywhere else the two frames are identical, so the split is
invisible there.

## How it was confirmed

Pulling frame 135 out of the recording and blowing up the right-hand edge
shows it in a single frame: the border sits at one x above the split and about
eight pixels to the left of it below. Not two ghosted lines, which is what a
camera blending two refreshes would give — one solid line that jogs, which is
what a torn frame gives.

## The fix

```cpp
::EndPaint(hwnd, &paint);
::DwmFlush();
```

`DwmFlush` blocks until DWM has finished composing its next frame. Since every
paint ends with it, every *following* paint's blit begins just after a
composition ended — with most of a frame interval to complete in. A blit of
the dirty rectangle takes a small fraction of that, so the compositor never
sees a half-written surface again.

This is the documented remedy for GDI drawing under the compositor, and it is
the same call the capture path already makes before it reads the screen.

## It makes the drag cheaper, not slower

Painting is now capped at the monitor's refresh rate rather than the mouse's
report rate. On a 165 Hz mouse against a 60 Hz panel, roughly two out of every
three frames were being composed and blitted only to be overwritten before
anyone could see them. Those are no longer drawn at all.

Mouse-moves coalesce in the queue while the flush waits, so the selection
still ends up exactly where the pointer is — you lose no positions, only
redraws nobody saw.

## Verification

Drag a region out quickly, in both directions, and watch the edge nearest the
pointer. It should stay a single unbroken line at every speed.

Worth checking on the NUC as well as the desktop: a slower machine takes
longer over the blit, which is exactly the condition that made the tear wider
before.
