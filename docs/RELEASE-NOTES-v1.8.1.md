# v1.8.1

Four corrections to 1.8.0. Three of them are features that worked exactly as
built and answered the wrong question, which is a more useful kind of bug
report than a crash.

---

## Keep the Editor on Top

**Pin to Screen used to float a separate copy of the picture.** A borderless,
always-on-top window holding the capture and nothing else — no toolbar, no
tools. It did what it said. It was not what pinning is for: the window worth
keeping in front of you is the one you are *annotating in*.

So the setting now makes the **editor** topmost, and is renamed accordingly.

It also applies to **full-screen captures**, which the first version excluded.
The reasoning then was that a full-screen capture pinned over the screen would
cover the thing it is a picture of — and that was simply wrong, because the
editor scales its capture down to fit a window. A pinned full-screen shot is a
window like any other.

Toggling it, from the tray row or the toolbar button, raises or lowers **every
open editor immediately** rather than applying to the next one you open. A
switch you can see the window obey is a switch; one that takes effect later is
a preference.

**A pinned editor is invisible to captures.** A topmost window is by
definition always in the way, and the one thing you cannot do about this one
is move it aside, because you pinned it there on purpose — so while the
setting is on, the editor is excluded from every region shot, full-screen shot
and recording the program takes. Turn the setting off and it goes back into
them, because an ordinary window has no business being invisible to the
recorder and you may well want to capture the editor itself.

`PinnedWindow` is deleted — about 550 lines, a window class, a registry of live
pins, and a second reap path through `App`. It is in the history if a floating
reference window is ever wanted as its own feature, which is what it should
have been.

---

## Redact folded into Shift-drag

**Shift-drag a Rectangle or an Ellipse and it fills** with the colour you
picked, exactly the way Shift-drag turns a Lift from a copy into a cut.

Redact was a tool whose only difference from Rectangle was the brush. That had
not earned a slot on the bar, and it forced a second hidden colour behind the
swatch — defaulting to black — so the one control on the bar that should
always mean one thing meant two.

Eight tools now, one swatch, one meaning. The canvas hint that used to appear
only for Lift now appears for all three tools that have a modifier:

| Tool | Hint |
|---|---|
| Lift | Drag to copy a piece · Shift-drag to cut it out |
| Rectangle | Drag for an outline · Shift-drag to fill it |
| Ellipse | Drag for an outline · Shift-drag to fill it |

**None of the security reasoning changed.** A flat fill is still the only safe
way to cover text — pixelation and blur both look like protection while leaving
the original recoverable, because a screenshot has a known font at a known size
and the attack runs the pixelation *forwards* over candidate strings. That
argument now lives on `Annotation::filled`, where the next person to wonder
"why not blur it?" will find it.

---

## The callout label moved to the tail

It was sitting **past the arrowhead** — on top of the very thing the arrow had
been drawn to single out. Exactly backwards.

It now sits behind the **tail**, in the empty space the drag started from, so
the arrow leaves the text and travels to the subject. Drag from where there is
room, towards the thing you mean.

The icon was redrawn the same way round — the letter first, the arrow leaving
it — because arrow-then-letter read as "the text is the destination", which is
the placement this release moved away from.

---

## The colour picker no longer covers the picture

It opened **upwards** from the swatch. The swatch is on the bottom bar, so
"upwards" is always over the canvas: over the capture, in the corner, while you
choose the colour you are about to draw on it with.

It opens **downwards** now, outside the window entirely. A popup is a top-level
window and is under no obligation to stay inside its parent, so below the
swatch is simply the desktop. It is also the direction the swatch's caret has
been claiming to open in since it became owner-drawn.

It flips back above the swatch only when the monitor's **work area** has no room
below — not the primary monitor and not the full monitor rectangle, so a window
sitting above the taskbar or on a secondary screen still gets this right.
Covering the canvas is the fallback rather than the rule.

> v1.7.7 moved the Lift *hint* out of the picker's way. That was a real
> collision, but it was not this one.

---

## Verification

1. Open the picker with the window at various sizes and positions, including
   dragged to the bottom of the screen. It should open below the swatch, and
   only flip above when there is genuinely no room.
2. Turn **Keep the Editor on Top** on from the tray. Any open editor should
   jump above other windows straight away. Turn it off; it should drop back.
3. Take a full-screen capture with it on — the editor should be topmost too.
4. Shift-drag a rectangle and an ellipse. Both should fill in the swatch
   colour. Drag without Shift; both should be outlines.
5. With Rectangle or Ellipse selected, check the hint reads *Shift-drag to fill
   it*; with Lift, the cut wording; with any other tool, nothing.
6. Draw a callout right-to-left and left-to-right. The label should sit behind
   the tail both times, and the text should not jump when you press Return.
7. Click a filled rectangle in its middle — it should select, where an outline
   one only selects on its edge.
