# v1.7.4

One fix: text in the annotation editor no longer appears to vibrate while you
draw.

## What was happening

The glyphs were never moving. They were being **resampled differently**.

The editor shows your capture scaled down to fit the window, and it redid that
scaling on every single mouse-move. The good resampler is too slow for that, so
the code switched to a crude one for the duration of a drag:

| Mode | What it does when shrinking |
|---|---|
| `HALFTONE` | **Averages** the source pixels that map to each destination pixel |
| `COLORONCOLOR` | **Drops** source rows and columns |

On text that difference is brutal, because a glyph stem is one or two pixels
wide — drop the wrong column and the stem of an `h` disappears, or shifts a
pixel. So the entire line of text changed appearance the instant you pressed
the mouse, and changed back when you released it. Two snaps in quick
succession, which reads as a vibration.

Stepping through a phone recording of it frame by frame shows it plainly: one
frame reads `The text normaliser's … Everything else is verified`, the next
reads `Ihe text normaliser's … Lverything else is verified`. Same pixels on
screen, different filter.

It has been there since the editor was written, which is why it is in every
build going back through the 09/24 backup.

## The fix

The capture is scaled **once**, to the size it is drawn at, and kept. So there
is no per-paint resampling for the crude mode to exist for, and the good one is
used always.

Drawing also gets **faster**, not slower — the scaling that used to happen on
every mouse-move now happens once.

## What was never affected

**Nothing you saved or copied.** Export re-blits the untouched original at full
resolution and redraws the annotations at scale 1, so every PNG and every
clipboard copy has always been pixel-exact from the capture. The wobble was
purely the on-screen preview.

**And it never showed on a small capture**, because one that fits the window is
drawn at 1:1 with no scaling at all.

## On the memory it costs

One canvas-sized bitmap — about **4 MB** for a typical window. It is bounded by
construction, and deliberately so:

- Drawing, undo, paste and save never touch it. Nothing is ever appended, so it
  cannot grow with use.
- A window resize **replaces** it, releasing the old one first — so shrinking
  the window shrinks this too, rather than leaving the larger one resident.
- It is **released entirely** when the capture is shown at 1:1, since then
  there is nothing to scale. A small capture costs nothing at all.
- It is freed when the editor window closes.

For scale: the editor already holds the full capture for the life of the
window, which is 33 MB for a 4K screenshot. This adds roughly a tenth of that,
and buys both the fix and faster drawing.

## Verification

Open a capture large enough that the editor scales it down — a full-screen shot
on a 4K display — and draw an arrow across some text. The text should stay
completely still through mouse-down, the drag, and mouse-up.

Then resize the window and draw again, since a resize is the one thing that
rebuilds the cache.
