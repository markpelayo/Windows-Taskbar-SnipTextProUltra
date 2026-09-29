# v1.8.2

Two fixes and two additions. The colour-picker fix is the one I got wrong
twice, so it is worth reading why.

---

## The colour wheel was escaping the picker

The tenth cell of the picker — the one that opens the full system colour
dialog — is drawn as six coloured wedges. Its radius was set to a **whole
cell** rather than half of one:

```cpp
const int radius = kCellSize;   // "overshoot, so wedges reach the corners"
```

`Pie` takes a bounding box, not a cell, and nothing clipped it. So the wheel
was drawn 16px past every edge of its 32px square. That square is the **last
column of the bottom row**, so the overflow left the popup altogether and sat
on the toolbar and the canvas underneath.

It has been wrong since the picker was written. The radius is now half the
cell less a two-pixel margin, which is also the conventional way to draw a
"custom colour" affordance.

## …and the picker opens upwards again

1.8.1 moved the picker *below* the swatch, outside the window, on the theory
that rising into the canvas was what made it cover the picture.

That was the wrong diagnosis. The picker was the right size and in the right
place; the wheel inside it was not. Opening downwards fixed nothing and put
the picker somewhere it does not belong — a swatch on the bottom bar opens
upwards, the way every other bottom-anchored menu on Windows does.

It is back above the swatch, still clamped to the monitor's work area so a
window dragged to the top of the screen cannot push it off the desk.

> For the record: v1.7.7 fixed a *different* real collision, the picker
> against the Lift hint. Three attempts, two of them at things that were
> genuinely wrong but were not what you were looking at.

---

## Esc closes the editor

A new row in **Change Keyboard Shortcut**, below the six capture shortcuts
and under a heading that reads *Only inside the editor*.

That heading is load-bearing. The six above it are **global**: claimed from
the whole system with `RegisterHotKey`, unavailable to every other program
for as long as this one is running. Doing that to a bare Esc would be a
catastrophe — no other application on the machine could use the Escape key.
So this action is never registered; the editor matches the keystroke itself.
It is rebindable and unbindable in exactly the same way regardless.

**It works whenever the editor is the active window**, regardless of what has
focus inside it — the canvas, a tool button, the width slider. That is not free: a keyboard message only ever reaches the control
with focus, and an editor is a frame full of controls, so a handler in the
canvas procedure meant Esc worked on the canvas and silently stopped working
the moment you clicked a tool button.

So the editor gets first refusal on keyboard messages from the message loop
itself, before they are dispatched. Reaching that hook at all means the
message is bound for this editor's window tree — and for keyboard input, that
is the same statement as "this editor is the active window". Pinned or not
makes no difference.

**Esc is a cascade, not one meaning:**

1. Cancel an in-progress label, if there is one
2. Otherwise clear the selection, if there is one
3. Otherwise close the window

A mistyped label costs one press, not the editor.

**No save prompt.** Nothing in the editor has been written to disk in the
first place, Copy and Save are one keystroke each, and a confirmation dialog
on a scratch window is the kind people learn to dismiss without reading. If
that is not the trade you want, unbind it.

Two consequences of being local rather than global, both of which were bugs
before they were decisions:

The capture window uses Esc to cancel, and separately refuses any unmodified
key that is not a function key — because registering a bare key globally
takes it from the whole machine. Both rules would have made Esc unbindable
here, so unbinding this action once would put its own default permanently out
of reach. Neither rule applies to an action that registers nothing, so both
are now conditional on the action being global, and the capture window's
footer says *click away to cancel* instead.

And running in the message loop rather than a window procedure is also what
makes Ctrl- and Alt-based bindings work at all. In the canvas procedure, the
Ctrl block returns unconditionally so a Ctrl binding was unreachable, and Alt
combinations arrive as `WM_SYSKEYDOWN`, which the canvas never handled.

---

## The pointer says what a click will do

The canvas showed a crosshair over everything, including the marks you were
trying to grab.

| Where the pointer is | Cursor |
|---|---|
| A corner handle | diagonal resize, matching the corner |
| A side, or top/bottom, handle | horizontal or vertical resize |
| A line or arrow endpoint | move |
| Any mark that would be picked up | move |
| Empty canvas, Text tool | I-beam |
| Empty canvas, any other tool | crosshair |

Two decisions worth stating.

**It is resolved in the same order `WM_LBUTTONDOWN` resolves a click** —
handles, then marks, then empty space. Any other order and the cursor would
be lying at exactly the boundaries where it matters most: on the edge of a
handle that overlaps the mark beneath it.

**Mid-drag the answer is frozen.** A move stays a move even if the pointer
wanders over a handle on the way, because the gesture has already been
decided and a cursor that flickers during a drag reads as a bug.

**Endpoints get the move cursor, not a diagonal.** A rectangle's corner
travels on an axis, so a diagonal arrow is true. A line's endpoint goes
wherever you put it, and implying a direction would be false.

**The crosshair stays for empty canvas**, because every tool but Text draws
by dragging, and a crosshair is a precision cursor that says "the mark starts
here". An arrow would say "click things", which is not what that space does.
Text is the exception: it places a caret, and an I-beam is what a caret looks
like before you put it down.

The canvas window class no longer carries a cursor at all — a class cursor is
applied by `DefWindowProc` before the window gets a say, which is why the
crosshair used to win everywhere.

---

## Verification

1. Open the colour picker. Every cell, including the wheel, should sit inside
   the popup's border. Check it against the toolbar and canvas behind it.
2. The picker should open **above** the swatch.
3. Press Esc in the editor with a label being typed (cancels the label), with
   a mark selected (clears the selection), and with neither (closes).
   Then click a tool button and the slider and press Esc after each — it
   must close the editor both times, pinned or not.
   Open the colour picker and press Esc: that dismisses the picker, and a
   second Esc closes the editor. The picker is an owned window rather than
   a child, so it is the one place Esc means something smaller first.
4. Change Keyboard Shortcut should show *Close the Screenshot Editor* under
   its own heading. Unbind it, confirm Esc no longer closes, then rebind it
   to Esc — that must be possible.
5. Hover a selected shape's corners, sides, and middle; hover an unselected
   mark; hover empty canvas with Text selected and with Arrow selected.
6. Start a drag and move the pointer across other marks mid-gesture — the
   cursor should not change until you let go.
