# SnipTextProUltra v1.9.3

**Shift now means one thing, on every tool, with no exceptions left.**

This release finishes something that took three releases, and it is worth
noticing that each of the three *removed* an exception rather than adding a
feature:

| | |
|---|---|
| **1.9.1** | Shift snaps **Line** and **Arrow** to 45°. |
| **1.9.2** | Shift squares **Rectangle** and **Ellipse** to 1:1 — and filling, which had briefly lived on Shift, moved to **Ctrl**. Filling is not a constraint. |
| **1.9.3** | Shift squares **Lift** and **Crop** too. Lift's cut-instead-of-copy, the last thing on Shift that was not a constraint, moved to **Ctrl**. |

There is now nothing to remember beyond two sentences:

> **Shift constrains the geometry.** **Ctrl changes what you get.**

---

## The one behaviour change: Lift drags now move

**This inverts behaviour that shipped and that you have been using**, so it
gets stated plainly before anything else.

| | Before (1.9.0–1.9.2) | Now (1.9.3) |
|---|---|---|
| **drag** | Copy — original stays | **Move** — source is blanked |
| **Ctrl**+drag | *(nothing)* | **Copy** — original stays |
| **Shift**+drag | Cut — source blanked | **Perfect square** selection |

### Why it was worth breaking

**1. Shift had to come free.** Shift now squares off on every tool that has a
shape to square. Lift cannot be the one tool where it means something else —
one exception is all it takes for a rule to stop being a rule, and then you
are back to memorising gestures per tool.

**2. `Ctrl`-drag-to-copy is what Windows already does.** File Explorer, every
list view, every canvas, every file manager on this operating system: drag
moves, `Ctrl`-drag copies. That muscle memory already exists. This borrows it
rather than competing with it.

**3. Plain drag *moves* things.** That is simply what dragging is. Lifting a
piece out and leaving a duplicate behind was the surprising default — safer,
but surprising, and it made Lift read as "stamp" rather than "lift".

### What you give up, and what answers it

The old default never left a hole, and the new one does: the source is filled
with the most common colour in a two-pixel ring around the region, which is
exact on a flat background and visibly a patch on a gradient or a photo.

Two things answer that. `Ctrl` copies, and never leaves a hole. And `Ctrl+Z`
still undoes a lift in a single step — as it always did, because **the capture
underneath is still never modified**. The blanked patch is part of the mark,
not a change to the pixels, so nothing is destroyed either way. If a lift
looks wrong, undo it or drag it back; there is nothing to recover.

---

## Shift for a perfect square: Lift and Crop

Hold **Shift** while dragging either one and the selection is constrained to
1:1, the same way Rectangle and Ellipse have worked since 1.9.2. It composes
with Ctrl, so `Ctrl+Shift`+drag on Lift is a square piece, copied.

Crop squares **in image pixels, after rounding**, not in screen pixels. A crop
is the one region you might actually measure afterwards, and two
independently-rounded edges can leave a "square" a pixel off square — so it is
squared as the last step before the crop becomes a rectangle, and it is exact
at any zoom level.

### The bug this nearly shipped with

Both of these tools clamp their region: Lift to the current crop, Crop to
itself. Neither clamp knew anything about squareness. So a Shift-drag that ran
into the grey letterbox around the picture would show you a square marquee and
then hand back a rectangle.

This is the kind of bug worth describing because of how badly it would have
gone unfound. The clamp does nothing at all unless the drag leaves the
picture, so the feature would have worked every time you tried it deliberately
and failed occasionally in real use, with no obvious pattern — rare,
intermittent, and almost impossible to attribute to the right cause.

The clamp now takes a `keepSquare` flag and **shrinks to the shorter side**.
Shrinking rather than growing is one subtlety: growing to the longer side
would push the region back outside the bounds the clamp exists to enforce,
which for Lift means a piece carrying cropped-away pixels into the saved file.

The other subtlety cost a second pass to find. Shrinking has to know **which
corner you are holding still** — the first version always trimmed away from
the top-left, which is right only if you dragged down and to the right. Drag
up-and-left into the letterbox and it would eat the corner under your cursor
and slide the square somewhere you never pointed at: the same bug all over
again, a square marquee committing as something else, only now as a position
error rather than an aspect-ratio one. The clamp takes the drag's origin and
pins the edges nearest it.

---

## Under the hood

`ToolIsClosedShape` was one predicate doing double duty — the tools that can be
squared *and* the tools that can be filled — on the reasoning that if those two
sets ever diverged it should be a deliberate edit. They have now diverged, and
this is that edit.

Shift's meaning lives in `Annotation.h` as `ToolSnapsToAxis` and
`ToolConstrainsToSquare`, whose union is exactly the set of tools that get a
canvas hint. Ctrl's lives in `ToolCanFill` and `ToolCopiesWithCtrl` — two
predicates rather than one, because unlike Shift, Ctrl really is two operations
under a single slogan.

Pen and Text appear in none of the four. A drag needs geometry before a
modifier can do anything to it, which is also why they are the only two tools
with no hint line — so the hint bar's mere presence tells you a modifier is
worth trying.

---

## Canvas hints

Every hint line now names Shift first and in the same position, because it
means the same thing on all six tools that show one. A hint bar that reads the
same way every time teaches the rule; one that varies teaches only the line.

| tool | hint |
|---|---|
| Lift | *Drag to move a piece · Shift for a square · Ctrl to copy* |
| Crop | *Drag to keep that area · Shift for a square · Nothing is lost* |
| Line, Arrow | *Drag to draw · Shift-drag to snap to 45°* |
| Ellipse | *Drag for an outline · Shift for a circle · Ctrl to fill* |
| Rectangle | *Drag for an outline · Shift for a square · Ctrl to fill* |

Crop's third slot deliberately does *not* say "Ctrl+Z undoes it", even though
it does. Every other line puts a Ctrl **drag modifier** there, so naming Ctrl
on the one tool where Ctrl-drag does nothing would invite you to try a gesture
with no effect. The reassurance is what mattered and it is still true.

As before, the **geometry** a modifier produces is read live during the drag
for the preview and again at mouse-up for the committed result, through the
same code, so the shape you see while dragging is the shape you get.

Two honest exceptions: Lift's Ctrl is read only at release and the marquee
cannot show it — a moved piece and a copied piece look identical until you
drag one aside. And a Shift-squared selection that runs into the letterbox
commits slightly *smaller* than the last frame you saw, which is the trade
described above.

---

## Upgrading

Nothing to do. No settings changed, no files moved, no stored state touched.

The only thing to unlearn is Lift: **drag moves, Ctrl copies.** The canvas says
so along the bottom whenever Lift is selected.
