# SnipTextProUltra v1.9.7

No new features. This is what a pre-release audit turned up: **five bugs, one
a regression from 1.9.6, and two of them able to lose your work.**

---

## 1. Selecting Arrow reverted to Rectangle — a 1.9.6 regression

Pick Arrow, close the editor, open another capture: you had Rectangle.

`ToolKeyValue` writes Arrow through its `default:` arm, so `"arrow"` is a
string that `ToolFromKeyValue` genuinely has to parse — and it had no branch
for it. That was invisible for as long as the *fallback* was also Arrow: the
missing case and the fallback happened to agree, so the bug was latent and
harmless for many releases.

1.9.6 moved the default to Rectangle and they stopped agreeing.

The second-order effect was worse than the first. `editor_settings::IsDefault`
compared `CurrentTool()` — now Rectangle — against the default, also
Rectangle, and concluded the settings were untouched. So with a stale
`"arrow"` still sitting in the registry, **Sanitize and Restore Default greyed
itself out**: the one menu item that would have cleared the bad value decided
there was nothing to clear.

A missing `if` in a function whose fallback covered for it. Exactly the kind
of thing that only surfaces when you change the thing it was leaning on.

---

## 2. Esc during a drag closed the editor and threw away everything

Start a crop marquee, or get halfway through dragging a box, think better of
it, press Esc — **the editor closed and every mark on the picture was gone,
with no prompt.**

Esc in the editor is a cascade: it backs out of the smallest outstanding thing
first — a label being typed, then a selection, then the window. A **drag in
flight** was missing from that list. And it could not fall through to the
selection rung either, because mouse-down clears the selection before starting
a Drawing drag. So it went all the way to Close, which deliberately does not
ask about saving.

Esc now cancels the drag. Implemented by calling `ReleaseCapture`, so it takes
the *identical* path that an Alt+Tab mid-drag already took — that case was
handled correctly all along, which is what made Esc the odd one out.

One honest caveat, since it is the same handler: for a drag that is *drawing*
something, Esc discards it outright. For a drag that is **moving or resizing**
an existing mark, Esc stops the drag and **keeps the move** — recoverable with
`Ctrl+Z`, which already has the snapshot. That is exactly what Alt+Tab has
always done. Making Esc revert a move instead would mean not reusing this
path, and is a separate change.

---

## 3. Rebinding a tool onto `Ctrl+Z` silently deleted undo

`Ctrl+Z`, `Ctrl+Y`, `Ctrl+C` and `Ctrl+S` are hard-coded in the editor's key
handler. They are not `hotkeys::Action`s at all, so the de-confliction pass —
which compares the sixteen rebindable actions against each other — could not
see them.

Bind Rectangle to `Ctrl+Z` and the rebind dialog accepted it without comment.
Then the editor-local dispatch won, because it runs from
`PreTranslateMessage`, *before* the keystroke is dispatched to the canvas at
all. **Undo was gone.** Nothing in the interface explained why, and the only
recovery was Reset to Defaults.

Now refused at capture time, with the same warning beep a bare key gets for a
global shortcut. Refused rather than de-conflicted, because the other side of
the collision is not a binding that can be moved out of the way.

The check covers every key the editor handles directly — `Ctrl`(+`Shift`) with
`Z`, `Y`, `C`, `S`, `Up`, `Down`, and bare `Delete`, `Backspace`, `F2` and the
arrows — and it applies to the **global** shortcuts as well as the local ones.
That last part is the opposite of the first version of the fix. The reasoning
that globals do not matter here ("the global fires first, so who cares") is
backwards: `RegisterHotKey` claims the combination system-wide and the
keystroke is then never dispatched to anybody, so a *global* on `Ctrl+Z` kills
editor undo by the same mechanism **and** takes `Ctrl+Z` away from every other
program on the machine. Strictly the worse case, and it was the one left
open.

---

## 4. A text label placed in the grey letterbox was unreachable forever

Click with the Text tool on the mat around the picture and you got a label at
coordinates outside the crop. It was then:

- clipped out of the canvas, so you could not see it
- clipped out of the export, so it never appeared in the saved file
- excluded from hit-testing by `IsWithinCrop`, so it could not be selected,
  moved or deleted

…while still riding along in every undo snapshot and every save. `Ctrl+Z` was
the only way to get rid of it.

Lift and Crop both clamp their region to the crop. Text did not. It now
**refuses** the click rather than clamping it: clamping would drop the label
somewhere you did not click, and a click on the mat around the picture is not
a request to annotate the picture.

---

## 5. A failed recording left a broken file behind

Only the zero-frame case deleted its output. A write error or a failed
`Finalize` reported the error and left a partial, unplayable `.mp4` in the
Videos folder — where it then counted toward the saved-video total in the menu
and turned up in the Sanitize list. The app was telling you that you had a
recording you could not watch.

Every failure path now removes the file. Same fix for a truncated `.png` from
a failed auto-save, which had the identical shape.

---

## New: a reference for the tray menu

[`docs/tray-menu.md`](tray-menu.md) lays the whole menu out as a text
skeleton — every row and submenu in order, with the labels that change
according to state, and what each one is for.

The menu is the entire interface outside the editor, and it had no reference
of its own: the README explained individual settings where they came up, and
nothing anywhere showed the *shape*. It also documents the two things about
how the menu is built that are easy to get wrong — it is rebuilt from scratch
on every open (so the saved-file counts cannot go stale), and its section
headers are disabled items because Windows has no header item type.

## The toolbar figure is redrawn

`docs/editor-toolbar.svg` and `.png` had not been touched since 1.8.2 and had
drifted badly: the old tool order, Arrow shown as the selected tool, the old
green swatch, and the long-removed Callout button. It needed a disclaimer
caption in both the README and ARCHITECTURE just to stop it contradicting the
prose above it.

Redrawn, and the caption is gone. It now shows the current order with
Rectangle selected, the `#007AFF` swatch, Crop in place of Callout, the hint
bar as it actually renders, and **each button labelled with its number key** —
which makes it a keyboard reference as well as a layout drawing.

---

## What was checked and found clean

Two full audits, one for resources and one for logic.

**No memory or resource leak that grows** per capture, per repaint, per undo
or per recording. Every GDI object, handle, `HGLOBAL`, COM pointer and
registry key is either owned by an RAII type or released on every path,
including the error paths. The undo and redo stacks are bounded together, the
scaled-image cache is replaced rather than grown, and the deferred delete of a
closed editor was verified on every route including quitting with an editor
still open.

Also clean: coordinate arithmetic and the crop offset (no division by zero, no
degenerate-rect crash), `selectedIndex_` lifetime across every mutation,
capture/recording re-entrancy, settings migration from corrupt or
hand-edited registry values, and the `/W4 /WX` pass.

## Known and deliberately not changed yet

Two things the audit raised that are **not** in this release, because both
deserve to be tested on their own rather than folded into a fix pass:

- **Opening a label field spends an undo step and clears the redo stack**,
  even if you press Esc without typing anything. So an accidental
  double-click on a mark kills redo. The fix restructures the label editing
  flow, which is worth doing carefully.
- **The recorder copies a whole frame into a fresh buffer every frame**
  (~33 MB per frame at 4K), and creates and destroys two GDI bitmaps per
  frame just to read the cursor hotspot. That is the single largest
  optimisation available in the program, and it is in the most fragile code
  in it.

## Upgrading

Nothing to do.
