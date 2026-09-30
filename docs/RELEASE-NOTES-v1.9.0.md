# v1.9.0

A label stopped being a kind of mark and became a property of every mark.
The Callout tool went with it.

## What you asked for, and what changed on the way

You wanted a rectangle that carries a line and a label as one object, with
the label snapping to eight directions when you drag it — and the same for
the arrow, using one arrow rather than a separate tool. You also spotted that
the modifier you had in mind was going to collide with something.

The collision was not quite where you thought: `Ctrl`+drag and `Ctrl`+arrow
*keys* never meet, one being the mouse and the other the keyboard. The real
problem was the modifier budget. Rectangle would have meant four things
depending on what you were holding at the moment you released — plain, Shift
for fill, Ctrl for label, Ctrl+Shift for both — and nothing on screen would
have told you three of them existed.

So the label is not a mode of a tool. **It is a property any mark can have.**

`Annotation` already carried a `text` field, used only by Text and Callout.
Letting every mark use it cost almost nothing structurally and gave, in one
move: labelled rectangles, labelled ellipses, labelled lines, labelled pen
strokes, labelled lifted pieces, and one arrow that can carry text.

## How it works

**Double-click a mark to label it**, or press **F2** with it selected. On a
Text mark the same key edits the text — it is the same operation, because
both are the string you typed.

Draw first, label second. Two deliberate steps instead of a modifier you have
to remember *before* letting go of the mouse — and it lets you label
something you drew five minutes ago, which the modifier version could not.

**Drag the label to swing it** around the mark. Eight directions: the four
sides and the four diagonals. The leader follows.

## The design decision that makes it simple

The label position is stored as an **angle index plus a gap**, not as a point.

```
int    labelAngle = 0;     // 0 = east, clockwise in 45-degree steps
double labelGap   = 24.0;  // image pixels past the mark's edge
```

Your instinct to reduce complexity paid off more than you may have expected.
Because the angle is an index:

- **There is no modifier.** The label is always snapped, so there is no Shift
  to hold and nothing to explain.
- **It cannot go stale.** The position is re-derived from the mark's current
  bounds every time it is drawn, so resizing a rectangle carries its label
  along and the leader can never end up pointing at nothing. A free point
  would have needed updating on every resize, move and undo.
- **An empty label removes it.** No separate "remove label" command.

The leader runs from where the ray leaves the mark's bounding box to the
label's near edge, so the gap you drag is the gap you see, in every direction.
That needs the *support function* of the label box — half its extent along
the ray — or a diagonal label would crowd the mark while a horizontal one
would not.

The leader is drawn at 60% of the mark's stroke weight. A leader as heavy as
the rectangle it points at competes with it, and the label is an aside.

## Callout is gone

It was an arrow that carried a label. An arrow with a label is exactly what
it was, so nothing is lost — one fewer tool, eight instead of nine, and the
minimum window width back to 502px. A saved tool of `callout` falls back to
Arrow, which is what it now is.

## The modifier budget is untouched

Shift still means fill on the closed shapes, cut on Lift, snap-to-45° on Line
and Arrow. Ctrl still means layering. Labels needed neither.

## Four things the review caught

**Grabbing a label teleported it.** `AimLabelAt` centres the box on the point
it is given, so touching the label anywhere off-centre snapped its middle
under the pointer. It now records where inside the label the grab happened.

**A label being edited was drawn twice** — once by the mark on the canvas,
once by the field on top of it — and as characters came and went the longer
committed one showed through behind. The text is now taken *off* the mark for
the duration of the edit and put back if you cancel.

**Cancelling an edit left a spent undo step.** The snapshot was taken before
the field opened, so Esc cost you one Ctrl+Z that visibly did nothing — and
worse, the snapshot had already cleared the redo stack. It now snapshots
*after* the field exists. Popping it back off was the first attempt and is
not safe: a push that hits the fifty-step cap erases the oldest entry, which
a pop cannot restore.

**A double-click banked a stray move.** The mouse-move between the two clicks
fired the drag path, nudging the mark a pixel and taking an undo step for it.
Drags now have to travel a minimum distance before they count — which also
stops every click-to-select risking the same.

## Verification

1. Draw a rectangle, double-click it, type a label. You should get the
   rectangle, a leader and the text.
2. Drag the text around it — all eight positions, leader following.
3. Resize the rectangle by a corner. The label should keep its direction and
   stay the same distance from the new edge.
4. Drag the *rectangle*. Everything should move together.
5. Change the colour and the width with it selected. All three parts should
   follow.
6. F2 again — the existing text should be there, selected, ready to replace.
7. Clear it and commit. The label and leader should go, the rectangle stay.
8. Ctrl+Z after adding a label should remove just the label, in one step.
9. Repeat 1–4 with an arrow, an ellipse, a line and a lifted piece.
10. Click the label of an *unselected* mark — it should select the object
    rather than swinging the label, since swinging needs it selected first.
11. Copy and Save should both show the leader and the label.
