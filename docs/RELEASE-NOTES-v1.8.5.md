# v1.8.5

The flicker, removed. Nothing else changed.

## It was not animation

Worth saying plainly, because "it blinks" and "it animates" look the same
from the outside: there is **no animation anywhere in this program**, none
was added in 1.8.4, and none has been removed here. What you were seeing was
the compositor showing a paint that was not finished yet.

## What was actually happening

Every one of those surfaces was painted in layers, straight to the screen.

The slider:

1. fill the background
2. draw the track
3. draw the travelled part over it
4. draw the thumb over that

Four passes over the same pixels, each one visible to the compositor. The eye
catches the intermediate states and reads them as a flash. The colour picker
was worse — ten cells, a six-wedge wheel and eleven outlines, all drawn
directly.

The swatch had a second cause on top: it was invalidated with *erase*
requested, so Windows filled the whole control with the background brush
before the owner-draw code ran and covered every pixel of it again.

## The fix is faster, not slower

Both now draw into an off-screen bitmap and blit it once.

That is **strictly less work than before**. The overlapping fills still
happen, but in memory, where nothing has to be composited and no intermediate
state is ever presented. The screen is touched exactly once per paint instead
of once per layer.

And the erase pass is gone: `InvalidateRect` asks for a repaint without one,
and the slider and picker refuse `WM_ERASEBKGND` outright. A control that
paints every one of its pixels has nothing to gain from being cleared first.

## On efficiency in general

You are right to push on this, and it is worth recording where the real costs
are so future changes can be judged against it.

The toolbar is **not** in a hot path. It repaints on user actions — a tool
change, an undo, a drag of the slider — not per frame. The thing that runs on
every mouse-move is the canvas, and none of this touches it.

So the buffering costs one bitmap allocation per paint of a 34 × 28 or
120 × 28 control, measured in microseconds, and saves several full-surface
composites. If anything here ever shows up in a profile, it will not be this.

## Verification

1. Drag the slider from one end to the other. The thumb should move cleanly
   with no flashing behind it.
2. Click the colour swatch repeatedly. Neither the swatch nor the picker
   should flash as it opens.
3. Switch tools quickly. The buttons should change state without blinking.
