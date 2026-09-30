# SnipTextProUltra v1.10.0

**You can now take another screenshot from inside the editor, and it lands in
the picture as a piece you can move.**

Minor rather than patch: the editor does something it could not do before, and
the window's minimum size changed.

---

## Snap another screenshot in

The last button in the mark-making group, and **`9`**.

Press it and the editor **hides itself from the capture**, the crosshair comes
up, and whatever region you drag lands in the middle of the current picture.
From there it is an ordinary mark — drag it, resize it from its handles, send
it behind or in front of other marks with `Ctrl`+arrows, delete it, undo it.
Cropping the picture clips it like anything else.

It is for the cases where one screenshot is not the whole story: a dialog and
the setting that caused it, a before and an after, an error and the log line
behind it. Previously that meant saving both and opening an image editor.

### Where it lands, and why

| | |
|---|---|
| **centred** | in the current picture, shrunk to fit with a margin |
| **never enlarged** | a small snap stays its own size |
| **shrunk when it must be** | a full-screen snap at 1:1 would cover everything |

*Inside* the picture rather than beside it, and that is not a stylistic
choice. A mark outside the crop is clipped out of the canvas **and** out of
the saved file, and is excluded from hit-testing — so it would arrive
invisible and unselectable. That is exactly the trap the Text tool had until
1.9.7, and there was no reason to build a second one.

Never enlarged because a snap smaller than the picture is almost always a
fragment of a window, and blowing it up to fill the canvas is not what anyone
means. Shrinking is different: a full-screen snap dropped in at 1:1 would
cover the picture completely, leaving nothing visible to grab.

### Two things worth knowing

**The editor hides from the capture only for the duration** — unless **Keep
the Editor on Top** is on, in which case it stays hidden afterwards. That
setting's whole bargain is that a pinned window does not turn up in your
captures, and this must not quietly undo it.

**Other editor windows stay capturable.** Only the one you pressed the button
in hides, so snapping a piece out of an earlier capture works.

**The original picture is never modified.** A snap is a mark sitting on top,
exactly like a lifted piece. `Ctrl+Z` removes it and nothing underneath was
touched.

### How it is built, and why that matters

A snap **is a Lift** — one that carries its own bitmap instead of reading from
the capture.

Everything a snap needs already existed on Lift: `source` is the rectangle the
pixels are read from, `start`/`end` are where they land. So moving, resizing,
hit-testing, the eight handles, z-order, undo, and the crop clip all work with
**no new code at all**. The only difference is which image the pixels come
from, and that is one pointer. A separate tool would have meant duplicating
every one of those behaviours in order to change it.

The bitmap is held by `shared_ptr`, and that is load-bearing rather than
tidy. An annotation is copied wholesale into every undo snapshot, so a
by-value bitmap would put 33 MB into *each step* of the undo stack — the same
trap that made the crop a 16-byte `RECT` over an untouched capture rather than
a cropped copy. A `shared_ptr` copy is a refcount, so fifty undo steps holding
the same snap cost one bitmap between them.

It is also cloned once, at capture time, into a bitmap that owns its own
pixels rather than borrowing the captured DIB's buffer. One copy per snap buys
a lifetime with nothing to reason about: the piece outlives the capture it
came from, survives being copied into snapshots, and lasts until the editor
closes.

---

## One toolbar, not two

Undo, redo, copy, save and pin moved down beside the tools. That left the top
bar empty, so it is gone — and its **44px went back to the canvas**.

```
swatch slider │ 8 tools + Snap │ Undo Redo │ Copy Save │ Pin
  how it looks│  make a mark   │  history  │  output   │ this window
```

The wider gaps between groups are the point. A button's neighbours now tell
you what kind of thing it is; fourteen evenly-spaced buttons would be a row
you have to read rather than one you can scan.

### The window minimum changed

**760 × 424**, was 502 × 468. Wider, because fourteen buttons on one line cost
width. Shorter by exactly the bar that went away.

The old floor needed two competing numbers, because the top bar's Pin button
was **centred** — centring is symmetrical, so the wider flank had to be
reserved on *both* sides, and the minimum was set by where the centre would
meet a flank. Nothing is centred now, so the row is simply a fixed width and
the floor is that width. One number instead of two, and no way for the groups
to collide.

---

## The Pin shortcut moved to `0`

So the digits run along the bar with no gap: **1–8** the tools, **9** snap,
**0** pin — the key past 9 on the row, for the one button that changes nothing
about the picture.

Only the *default* moved. If you had already rebound Pin, you keep what you
chose: a stored binding always wins over a default.

---

## Also

`docs/editor-toolbar.svg` and `.png` redrawn for the single row, with the five
group brackets labelled and every button carrying its number key. The new Snap
glyph is a dashed frame with a plus in it, sharing the marquee that the region
overlay and the Lift glyph both use — Lift takes a piece *out* of this
picture, Snap brings one *in* from the screen. Deliberately not a camera: a
camera would say "take a screenshot", and the half that matters here is that
the result lands in *this* picture.

`docs/tray-menu.md` and the README's shortcut list updated for the
seventeenth binding.

---

## Known limits

**A snap cannot be cropped on its own.** Resizing it scales the whole piece,
and the Crop tool crops the *picture*, clipping every mark including this one.
Trimming a snap's own edges would need a second kind of resize gesture — one
that changes `source` instead of `start`/`end` — and that is a separate piece
of design rather than something to bolt on here. If you want it, say so.

## Upgrading

Nothing to do, with one thing to know if you had rebound an editor shortcut.

The toolbar has moved, which you will notice immediately, and the editor
window may open wider than before on a small capture.

**If you had bound some editor action to a bare `9` or `0` in 1.9.7**, it now
shares a key with Snap's or Pin's new default. Nothing is checked at startup —
the de-confliction pass only runs when you change a binding through the menu —
so the tool wins (it is matched first) and Snap or Pin is silently unreachable
by keyboard. The *buttons* still work. Rebinding either one in *Change
Keyboard Shortcut* fixes it, and doing so runs the de-confliction pass.
