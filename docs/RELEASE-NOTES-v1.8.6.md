# v1.8.6

Straight lines, and layering.

## Shift snaps Line and Arrow to 45°

Hold Shift while drawing and the line locks to the nearest of the eight rays
— horizontal, vertical and both diagonals, in every direction.

It is read **live**, on every mouse-move, so the line straightens the moment
Shift goes down and springs back the moment it comes up, without releasing
the button. It also applies when re-aiming an existing line by its end
handle, because that is the same gesture with a different starting point.

**By projection, not rotation.** The snapped end is where the cursor falls
perpendicular onto the chosen ray:

```
end = start + ((cursor - start) · u) · u        u = the unit vector of the snapped ray
```

The alternative — keeping the distance and rotating to the nearest ray —
makes the endpoint swing away from the pointer at a fixed radius, which
feels like the line is fighting you. With projection, dragging roughly east
gives an end that tracks the cursor horizontally with its height pinned,
which is what the gesture is expected to feel like.

Below about a pixel of travel there is no direction to snap to, and `atan2`
of nearly-zero is noise that would flick the line between axes — so short
drags are left alone.

**Line and Arrow only**, as asked. Pen is freehand by definition, and
Rectangle and Ellipse already use Shift for filling.

## Layering, and why not bare arrow keys

You suggested up and down arrows. I would push back on that one, and here is
the reasoning rather than just the verdict.

In every editor people have used — PowerPoint, Illustrator, Figma, Paint.NET
— a **bare arrow key nudges the selection by a pixel**. It is the more common
need of the two, and it is the first thing anyone tries when a mark is a
little off. Spending bare arrows on layering would take that away and
surprise people who expect it.

So:

| Key | What it does |
|---|---|
| **Ctrl+Up / Ctrl+Down** | Forward / back one step |
| **Ctrl+Shift+Up / Down** | All the way to the front / back |
| **Arrow keys** | Nudge one image pixel |
| **Shift+arrow** | Nudge ten |

Ctrl+arrows keeps your instinct — up means up — while leaving bare arrows for
the thing they mean everywhere else. It is also layout-independent, which
`Ctrl+[` and `Ctrl+]` (the Adobe binding) are not: those are OEM keys whose
position changes with the keyboard layout.

If you would still rather have bare arrows do the layering, it is a two-line
change and nudging can move to Ctrl instead. Say the word.

**Up is towards you**, matching "bring forward". The annotation array *is*
the z-order — marks are drawn in order and hit-testing walks it backwards so
the topmost is found first — so moving a mark forward is moving it to a
higher index. There is no separate depth value to keep in step, which is why
the layering cannot drift out of sync with what is on screen.

Nudging is in **image** pixels, not view pixels: a nudge on a capture shown
at half size should move the mark one pixel in the file, not two. It also
coalesces its undo steps the way the slider does, so holding an arrow key
down does not bury the undo stack under auto-repeat.

## Verification

1. Draw a line, then hold Shift mid-drag — it should straighten without you
   releasing the button, and un-straighten when you let Shift go.
2. Check all eight directions, and check the arrowhead still points the
   right way on a snapped arrow.
3. Select a line, grab its end handle, hold Shift, and swing it around.
4. Draw an arrow over a filled rectangle, select the rectangle, Ctrl+Up
   twice — it should pass in front of the arrow.
5. Ctrl+Shift+Down on it should send it behind everything.
6. Ctrl+Z after each: layering should undo like any other edit.
7. Nudge with arrows, then Shift+arrows. Hold one down — the repeat should
   produce one undo step, not fifty.
