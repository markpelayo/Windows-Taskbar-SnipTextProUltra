# v1.7.8

The last release of the 1.7 line, and the build to keep as a known-good
reference before the 1.8 interface changes. One fix, backported from 1.8.2.

## The colour wheel was escaping the picker

The tenth cell of the colour picker — the one that opens the full system
colour dialog — is drawn as six coloured wedges. Its radius was set to a
**whole cell** rather than half of one:

```cpp
const int radius = kCellSize;   // "overshoot, so wedges reach the corners"
```

`Pie` takes a bounding box, not a cell, and nothing clipped it. So the wheel
was drawn 16px past every edge of its 32px square. That square is the **last
column of the bottom row**, so the overflow left the popup altogether and sat
on the toolbar and the canvas underneath it.

The comment was half right about the intent and wrong about the arithmetic:
reaching the corners of a 32px square needs a radius of about 23, not 32, and
even that would have needed clipping to stay in the cell.

It has been wrong since the picker was written — every 1.x release has it.
The radius is now half the cell less a two-pixel margin, which is also the
conventional way to draw a "custom colour" affordance, so the circle sits
inside its cell the way every other swatch does.

## Nothing else

Deliberately. This is a maintenance release on the 1.7 branch so there is a
final 1.7.x binary with every known fix in it, while 1.8 changes the editor's
toolbar, tools and shortcuts. If something in 1.8 turns out wrong, this is
what to fall back to.

## Verification

Open the colour picker and look at the bottom-right cell. The wheel should
sit inside the popup's border, with no part of it over the toolbar or the
canvas behind.
