# v1.9.2

Shift gives you a perfect square and a perfect circle. Filling moved to Ctrl
to make room, and that trade is the whole of this release.

## The collision

Shift was already taken on Rectangle and Ellipse — it filled them. So this
could not simply be added; something had to move.

It is the fill that moved, and not reluctantly. **Constraining proportions is
what Shift means in every drawing application** — Photoshop, Illustrator,
PowerPoint, Figma, Paint. It is one of the most universal shortcuts in
graphics software, and it is what your hand already expects before you have
read anything. Filling, by contrast, is almost never a modifier anywhere,
because it is a *style* rather than a gesture.

Keeping that convention intact is worth more than any single gesture,
including one already shipped and tested.

## Where everything lives now

| | drag | Shift | Ctrl | Ctrl+Shift |
|---|---|---|---|---|
| **Rectangle** | outline | **square** | filled | filled square |
| **Ellipse** | outline | **circle** | filled | filled circle |
| **Line / Arrow** | free | snap to 45° | — | — |
| **Lift** | copy | cut | — | — |

The point of this table is that it does not need to be memorised. **Shift
constrains the geometry. Ctrl fills.** Two independent switches, so the
fourth column follows from the second and third rather than being a third
thing to learn — which is exactly what four unrelated modifier behaviours
would have been.

Ctrl-drag was genuinely free: Ctrl+*arrows* is layering, and that is the
keyboard.

**Lift is the one exception.** Its Shift cuts instead of copying, which is
not a constraint at all. It is a different kind of operation — it moves
pixels rather than drawing a shape — and it carries its own hint line, so it
says so rather than surprising anyone silently.

## Two details

**The side is the larger of the two spans.** Dragging out a 200 × 60 box with
Shift held gives a 200 × 200 square, not 60 × 60. The shape grows to contain
the drag rather than shrinking to fit inside it, so it keeps up with the
pointer instead of lagging behind whichever axis happens to be dominant. The
signs are preserved, so it still opens in the direction you are pulling.

**The fill now shows in the preview.** The old Shift fill was only applied at
mouse-up, so the preview was always an outline and you found out what you had
made after you had made it.

Getting that promise right took one more step than expected. Reading the
modifiers on mouse-move and again at mouse-up is not enough: press Shift and
then release the button **without moving the pointer**, and no mouse-move ever
arrives — so the last frame you saw was a square and the mark you committed
was a rectangle. The modifier keys now drive the preview themselves, on both
key-down and key-up, re-resolving against the last pointer position. All three
paths call the same two functions, so the preview and the commit agree by
construction rather than by inspection.

## Verification

1. Rectangle: drag, then Shift-drag. The second should be square whichever
   direction you pull, and should square up *while* you drag rather than on
   release.
2. Press and release Shift mid-drag a few times. It should snap between
   square and free with no lag.
3. Ctrl-drag: filled, and filled *in the preview*, not just after.
4. Ctrl+Shift: a filled square. Same for the ellipse — filled circle.
5. Drag out a wide flat box with Shift: the square should take the *wide*
   dimension.
6. Check the hint line changes between Rectangle and Ellipse — it should say
   "square" for one and "circle" for the other.
7. Line and Arrow should still snap to 45° on Shift, and Lift should still
   cut on Shift.
