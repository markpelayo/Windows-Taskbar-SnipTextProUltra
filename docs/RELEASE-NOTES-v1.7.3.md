# v1.7.3

An optimisation release, and the menu-fade wait removed. No new features, and
nothing here is meant to behave differently — only to do the same thing faster
and with less.

## The menu-fade wait is gone; captures are instantaneous again

1.7.2 slept 250 ms before a menu-initiated capture, to outlast the Windows
"Fade out menu items after clicking" effect. Two things were wrong with it.

It was gated on the wrong setting. `SPI_GETMENUFADE` is *"Fade or slide menus
into view"* — the fade **in**. The effect that causes the problem is
`SPI_GETSELECTIONFADE`. So switching the offending checkbox off did not switch
the delay off, and the tool felt slow for no benefit at all.

And 250 ms was not enough anyway — a faint menu still reached the capture.

Rather than correct the constant and raise the delay, all of it is removed. An
instantaneous capture is worth more than an occasional faint menu on one of two
paths, and the keyboard shortcuts were never affected in the first place. The
workaround is in the README's troubleshooting section: use the shortcuts, or
untick that one visual effect.

## Every release so far was built without whole-program optimisation

`build.bat` has passed `/GL` and `/LTCG` since the beginning. `CMakeLists.txt`
never did — and CI builds with CMake. So anyone building locally got the
faster, smaller binary, and **every published release did not**: no cross
translation-unit inlining, no cross-TU COMDAT folding. Fixed, in Release
configurations only.

## Faster

**The pixel loops are a word at a time instead of a byte at a time.**
`Bitmap::MakeOpaque` was one single-byte store per pixel at a stride of four,
which the compiler cannot vectorise — 16.6 million scattered writes on a
dual-4K grab, measured at 8–25 ms. It runs on **every** capture, every OCR
retry pass, and every recorded frame. The OCR inverter had the same shape. Both
rewrites were verified bit-identical across 200,000 random pixels plus the
edge cases.

**The selection overlay and the editor canvas reuse their paint buffer.** Both
allocated a bitmap the size of the repainted region on *every* `WM_MOUSEMOVE`.
The allocation plus first-touch page faults across that region was the largest
single slice of the per-move cost — roughly a fifth to a third of it, more on a
slow machine, and on a large selection that region approaches the whole
desktop.

**`FillAlpha` stopped creating and destroying GDI objects per call** — a device
context and a 1×1 bitmap, four to six times per paint, in a function whose own
comment said one per paint would be wasteful.

**The menu no longer walks three folders on every open.** It asked all three
for their file counts each time it was built, including the one shown at
launch. Fine on a warm local disk; the default folders are Pictures and Videos,
which Windows 11 frequently redirects into OneDrive, where a directory walk is
a network round trip. Measured at 100–600 ms for three folders with a couple of
thousand files.

Counts are now cached against the directory's last-write time, and counted
without building a path per file. Two things were needed to keep them honest:

- A zero or never-advancing timestamp — FAT, exFAT volume roots and some SMB,
  WebDAV and MTP redirectors all report one — is rejected. Keying a cache on a
  constant would have frozen the number for the life of the process.
- Changes this program makes itself invalidate the cache explicitly rather than
  waiting for the filesystem. A directory's timestamp lives in its parent's
  entry and is flushed lazily, so waiting would have resurrected exactly the
  stale-count bug that the live counts were built to prevent.

## Fixed

A GDI bitmap leaked on the path where `CreateDIBSection` returns a handle with
a null pixel pointer.

## Not done — and where the remaining size is

The executable is about 15 MB. **Two thirds of that is the static Tesseract
stack**, not the trained model, which is 3.9 MB.

An audit identified roughly **3–5 MB** that could come off by building
Tesseract with `DISABLED_LEGACY_ENGINE` (this program uses `OEM_LSTM_ONLY`
exclusively), without curl and libarchive (the model is handed over from
memory, and the program promises it never touches the network), and Leptonica
without any image-format codecs (the PIX is built by hand with `pixCreate`, and
no `pixRead`/`pixWrite` is ever called).

It is not done here because it needs a vcpkg overlay port and, more
importantly, it changes how a third-party OCR library is compiled. That has to
be *proved* rather than assumed: build it, then re-run a set of captures
through the Tesseract engine and diff the recognised text against this release.
Getting it wrong would silently degrade the feature this program exists for.

Also looked at and deliberately left alone: caching the composite of committed
annotations in the editor (the largest remaining interactive win, and the one
most likely to produce a stale canvas across undo, restyle and delete), and
moving PNG encoding off the UI thread (worth 250–600 ms on a 4K auto-save, but
it reorders the failure message relative to the editor appearing).

## Verification

The text normaliser's tests pass. Everything else is verified by use, and none
of this has been through a compiler on the author's machine.

What to check, in order of how likely it is to show a mistake:

- **A capture still looks right.** The two pixel-loop rewrites touch every
  captured image; a wrong one would show as a colour or transparency fault.
- **Dragging a selection, and drawing in the editor**, both of which now reuse
  a buffer — a mistake there would show as stale pixels or a trail.
- **The saved-file counts in the menu.** Take a screenshot with auto-save on
  and confirm the count moves immediately; delete one in Explorer and confirm
  it moves again.
- **The dimmed overlay still looks correct**, since `FillAlpha` now reuses its
  source bitmap.
