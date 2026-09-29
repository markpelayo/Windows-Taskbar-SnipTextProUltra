# v1.8.3

One addition: **Crop**.

## What it does

Select the tool, drag a rectangle, and that region becomes the picture — on
screen, and in whatever Copy and Save produce.

- **Marks survive it.** A mark straddling the new edge is clipped where the
  picture is and stays fully editable: selectable, movable, deletable.
- **Marks outside it survive too.** They are not drawn, but they still
  exist, and undoing the crop brings them back exactly where they were.
- **Ctrl+Z undoes a crop**, the same as it undoes a mark.
- **It is a one-shot.** The tool reverts to Arrow once the crop lands.
- **You can crop again**, as many times as you like. Each one narrows the
  view further; none of them can widen it, because widening is what undo is
  for.
- **The title follows the crop**, so it always reports what you would get if
  you saved right now.

## The design decision worth knowing

The obvious implementation is to cut the bitmap down, shift every mark by
the crop origin, and be done. It is also the wrong one, for a reason that
only shows up later.

Undo in this editor works by snapshotting state. If a crop replaces the
capture, then undoing a crop has to restore the capture — so **every undo
step would have to carry a bitmap**. A 4K capture is 33 MB. The undo cap is
fifty steps. That is a gigabyte and a half of undo history for a screenshot
editor, and Mark has been explicit from the start that this thing has to run
on low-spec machines.

So the crop is stored as a **rectangle**. Sixteen bytes.

```
crop_ : RECT, in original capture pixels
```

The capture is never modified. The canvas blits a sub-rectangle of it; the
export creates a crop-sized bitmap and blits the same sub-rectangle;
annotations keep the coordinates they were drawn in and are offset by the
crop origin only at the moment they are drawn.

Three things fall out of that for free:

1. **Undo is trivial** — put the old rectangle back.
2. **Marks cannot drift.** Storing marks in original coordinates means
   repeated crops cannot accumulate a rounding offset, because no mark is
   ever rewritten.
3. **Nothing is destroyed**, so "undo the crop" genuinely restores the
   picture rather than approximating it.

The crop enters the coordinate system in exactly one place — `ToImagePoint`
and `ToViewPoint` — which is the payoff of the editor having funnelled every
view↔image conversion through that pair from the beginning.

## Four details that would have been bugs

**The annotation offset must subtract the crop origin.** GDI+ maps a point as
`p * scale + offset`, and marks are stored in original capture coordinates —
so without subtracting `crop_.topLeft * scale`, every mark would be displaced
from the picture while its selection handles, which take a different path,
stayed correct. Marks would separate from their own handles and the canvas
would disagree with the exported file. Invisible unless you crop from
somewhere other than the top-left corner.

**The clip has to be released before the selection chrome.** Outlines sit
outside a mark's box, handles further out again, and the canvas hint bar
lives in the letterbox rather than in the picture — leaving the clip on
would slice the grips off any mark touching the crop edge and delete the
hint entirely.

**Hit-testing and the cursor skip marks outside the crop.** Otherwise an
invisible mark stays clickable out in the grey letterbox: the cursor turns
to the move shape over apparently empty space and a click selects something
you cannot see. Invisible and unreachable have to mean the same thing.

**The Lift clamp is against the crop, not the capture.** A lift starting in
the letterbox would otherwise carry cropped-away pixels back into the
exported file, where there is no clip to hide them.

## Two more

**The scaled-image cache is keyed on the crop**, not just on the destination
size. Two different crops can land on the same on-screen size, and without
the crop in the key the canvas would keep showing the region it was scaled
from.

**Annotation drawing is clipped to the visible picture.** Marks are in
original coordinates and a crop does not move them, so one that now sits
outside the crop would otherwise be drawn on the grey canvas *beside* the
image — visible, unreachable, and clearly wrong. Clipping is also what makes
a mark straddling the edge look cut off rather than floating.

## The window minimum goes back to 540px

Nine tools rather than eight. Still below the 700px the seven text-labelled
tools needed.

## Verification

1. Draw an arrow, a filled rectangle and a text label. Crop through the
   middle of them. All three should be cut off at the new edge, and all
   three should still be selectable and movable.
2. Drag one of them partly back outside the crop — it should clip live.
3. Ctrl+Z. The crop should come back to full size with every mark intact,
   including any that were fully outside.
4. Ctrl+Y to redo the crop.
5. Copy, and paste somewhere — you should get the cropped region, not the
   whole capture.
6. Save, and open the file — same.
7. Crop, then crop again inside that. Then undo twice.
8. Try a tiny drag with Crop selected. It should do nothing rather than
   leave you with a sixteen-pixel window.
