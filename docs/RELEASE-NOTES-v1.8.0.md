# v1.8.0

Three additions and a rebuilt toolbar. A minor version rather than a patch
because this is the first release that changes what the editor looks like.

---

## Pin to Screen

A new row in the tray menu, directly below Auto-Save Images.

With it on, **Screen Capture a Region** sticks the capture to the screen in a
floating, always-on-top window instead of opening the editor. An error
message, a part number, a diagram from a manual — it stays visible while you
work in something else, which is the one thing a screenshot on the clipboard
cannot do.

**Region only.** A full-screen capture pinned on top of the screen would
cover the thing it is a picture of.

The pin opens **exactly over the region it was cut from**, so it appears to
lift off the screen in place rather than materialising somewhere else and
making you go looking for it.

| Gesture | What it does |
|---|---|
| Drag anywhere on it | Move it |
| Scroll wheel | Zoom, around the pointer — the pixel under the cursor stays under the cursor |
| Double-click | Open it in the editor |
| Right-click | Open in Editor · Copy · Save… · Actual Size · Close Every Other Pin · Close |
| Esc | Close the focused pin — the one you last clicked |

Handing a pin to the editor gives the editor a **copy**, so closing the pin
afterwards takes nothing away from it.

You can have as many as you like. The tray menu grows a row telling you how
many are up and offering to close them all.

### What it replaces, and what it does not

It replaces **the editor**. Auto-Save has already written to disk by then,
and "Copy to Clipboard and Close" still copies — a pin does not stop you
pasting. But opening the editor *and* floating a copy of the same picture
would be two answers to one question.

Because Pin only applies to region captures, the editor can still open with
the setting on — from a full-screen shot, or from a pin's own "Open in
Editor". So the editor's top bar carries the switch as well, centred, and it
writes the same registry value the tray row writes. Toggle either one and the
other repaints, along with every other open editor: one setting, two
switches, and no window left showing the state the other just changed.

It is **icon-only**, like everything else on the bar, so the whole weight of
"is this on?" falls on the button's appearance: switched on it takes the same
filled face and doubled accent ring that a selected tool takes. One visual
language across both bars.

The tooltip carries the state in words — *Pin to Screen: On — a region
capture will stick to the screen instead of opening the editor. Click to turn
off.* — and is **rewritten on every toggle**. With nothing on the button face
to read, a tooltip still saying "Off" after you switched it on would be the
only thing on screen contradicting the button.

> If you *do* want "pin this one, now" as an action, that is a different and
> genuinely useful feature — say the word and it can be a second button. It
> is not this one.

### Pins are invisible to captures

A pin is excluded from every capture the program takes, the same way the
recording indicator is. Without that you could not screenshot or record the
area a pin was sitting over — and the reason you pinned it is usually that
you are about to work on what it is covering.

---

## Redact

Drag a rectangle; it fills solid and opaque. Black by default. The swatch
changes it while Redact is selected, which is useful for matching a
background, but that has to be a decision rather than a default.

**It is a flat fill and nothing cleverer, on purpose.** The first version of
this was going to pixelate, and that was wrong:

Pixelation and blur both *look* like protection while leaving the original
recoverable. A screenshot has a known font at a known size, so nobody has to
invert the averaging — they run it forwards. Render a candidate string,
pixelate it on the same grid, compare. It does not even explode
combinatorially, because each character is pinned by the few blocks it
touches, so it solves left to right one glyph at a time. Published tooling
has been reading pixelated text since 2022.

A flat fill is the only version whose output does not depend on the pixels
underneath. There is nothing left to work back from.

Redact is an annotation like every other mark, so it moves, resizes, restyles
and undoes, and the capture underneath is never modified until you export.

> **One thing worth knowing.** If **Auto-Save Images** is on, the untouched
> original was written to disk the moment the shot was taken — before you
> redacted anything. Redact, save, send the clean copy, and the readable
> original is still sitting in your screenshots folder. That is not a bug in
> Redact and no amount of drawing fixes it.

---

## Callout

Drag an arrow, then type a label that sits at its tip.

One annotation rather than an arrow plus a separate text mark. That matters
for the boring reason: moving it moves both halves, and the label cannot be
left behind pointing at nothing.

The label sits on the **far side of the head**, and flips to the left when
the arrow points left, so it never covers the thing being pointed at. For a
left-pointing callout the label is positioned by its right edge, which means
its left edge moves with every character typed — the edit field follows, so
the text does not jump when you commit it.

**Esc keeps the arrow and drops the words.** You drew the arrow deliberately;
changing your mind about the wording should not take it away. It is also a
separate undo step from the label for the same reason.

---

## The toolbar

Everything on it is an icon now, top row included.

| | |
|---|---|
| **Left** | Undo · Redo |
| **Centre** | the Pin to Screen toggle |
| **Right** | Copy · Save |

Fourteen buttons, all 34 × 28, all drawn by one function — the top row and
the bottom row are the same kind of control at the same size, so they are no
longer two toolbars that happen to be touching. Save lost the permanent
accent ring it inherited from `BS_DEFPUSHBUTTON`; the only buttons that get a
ring now are the ones that are switched **on**, which is the selected tool
and Pin when enabled. That is the only state which survives letting go of the
mouse, so it is the only one worth marking.

Left to right that reads: what you did, what will happen next, where it goes.
Each group is anchored to its own edge, so widening the window only grows the
gaps between them — they are positioned independently and cannot be pushed
into each other.

**They can only collide by the window getting too narrow**, and that is a
number, not a z-order problem:

```
34 (pin)  +  (82 flank + 12 gutter) × 2  =  222
```

The centred control has to clear the wider flank on *both* sides, because
centring is symmetrical — here both flanks are two icon buttons, so they are
equal by construction. `WM_GETMINMAXINFO` refuses to let the window go below
the floor, so there is no clamping in the layout code and no overlap to fix.

222 is far under the tool row's 540, so for the first time since the editor
was written the **bottom** row sets the floor:

| | Minimum window width |
|---|---|
| Seven text tools, split command bar | 700px |
| Nine icon tools, text command group | 678px |
| Nine icon tools, three anchored groups | **540px** |

Narrower than it has ever been, with two more tools than it has ever had.

![The editor toolbar, drawn to scale](editor-toolbar.png)

Every glyph is drawn in GDI from lines, arcs and Béziers. There is no image
resource anywhere in this program and adding one for this would have been the
first. `ExtCreatePen` rather than `CreatePen`, because round caps and joins
are the difference between a drawn arrow and a bundle of sticks.

The tool buttons became owner-drawn, which removed a duplicate copy of state:
the old `BS_AUTOCHECKBOX | BS_PUSHLIKE` pair kept "which tool is selected"
inside the control as well as in `currentTool_`, and two copies of one fact
is one too many.

---

## Verification

**Pin to Screen**

1. Turn it on. Take a region capture. It should stick where you dragged it,
   and no editor should open.
2. Drag it to a second monitor; scroll to zoom in and out; check the pointer
   stays over the same pixel as you zoom.
3. With a pin up, take another region capture over the top of it — the pin
   should not appear in the result.
4. Double-click it, annotate in the editor, then close the pin. The editor
   should be unaffected.
5. Turn Auto-Save on as well and confirm both happen.

**Redact**

6. Cover something, then drag the mark aside — the original text should be
   underneath, untouched, because nothing is baked in until export.
7. Copy and Save should both produce a flattened image where it is gone.
8. With Redact selected the swatch should show black; switch to Arrow and it
   should show your drawing colour again.

**Callout**

9. Drag right-to-left and type — the label should stay put as you type, not
   jump when you press Return.
10. Press Esc mid-label: the arrow stays, the words go.
11. Undo once after committing a label: the whole callout should go, not just
    the text.

**The bar**

12. Shrink the window to its minimum. Nothing should clip or overlap on
    either row — in particular the centred Pin toggle should keep clear of
    Undo/Redo on the left and Copy/Save on the right.
13. Hover every icon, top row included, and check the tooltip names what you
    expected.
14. Open two editors. Toggle Pin in one; the other should update immediately,
    and so should the tray menu's tick.
15. Toggle Pin from the tray with an editor open — it should update there too.
