# v1.6.2

One bug that only appeared on slower machines, a setting that explained itself
badly, and the executable now carries its version in its name.

## Fixed

**The flyout menu could end up inside the capture.**

The menu is dismissed and its `HMENU` destroyed before the command runs — but
destroying a menu does not put the pixels back. The desktop has to be
composited again without it. On a fast machine that happens before the capture;
on a slower one it did not, and the row that had just been clicked appeared in
the screenshot.

This was worse than cosmetic for *ScreenshotToText*, because the row's own text
— `ScreenshotToText a Region…   Ctrl+Shift+3` — was then recognised and copied
along with everything the user actually wanted.

Region captures made it easy to miss the cause. They are not photographed when
you finish dragging; they are cropped from a desktop snapshot taken the instant
the overlay opens, so the menu's pixels were baked in at that moment and the
crop merely revealed them.

The capture now waits on `DwmFlush` before reading the screen. Under DWM the
windows underneath never need to repaint — their content was never destroyed,
which is the point of redirected rendering — so all that is missing is a new
composition without the menu in it, and `DwmFlush` blocks until DWM has
finished composing. That is the compositor reporting the frame is done rather
than a guess about how long it takes. A sleep would have been wrong in both
directions: too short on the slowest machine it has to work on, and wasted time
on every machine faster than that.

Two flushes, because the first can return at the end of a composition that
began before the menu went away.

## Changed

**The Text Layout submenu describes both options, not one.**

It carried a single footer explaining only *Rebuild Paragraphs*. Selecting
*Keep Every Line Separate* left the menu still explaining the option you had
moved away from, so the only way to learn what the other one did was to pick it
and take a capture. Each option now has its description directly beneath it.

This is the third time this setting has been reworded, and each time the fault
was the same shape: the interface explained one state and left the other to be
inferred.

```
✓ Rebuild Paragraphs
      Rejoins sentences that wrapped, and puts
      a blank line between blocks.
  ────────────────────────────────
  Keep Every Line Separate
      One line of text for every line on screen.
      Nothing joined, nothing inserted.
  ────────────────────────────────
  Compare the Two on Sample Text…
```

**Compare the Two on Sample Text** opens a page of eight deliberately awkward
cases: a wrapped paragraph, a three-column table, a column of serial numbers, a
list whose items wrap, an indented block, a symbol string. Each says what to
expect from *both* settings, which makes it a regression check as well as an
explanation — if a column of serial numbers comes back joined into one line,
that is a bug rather than a preference.

The page is `docs/text-layout-test.html`, linked from the top of the README.
**The menu's link is a placeholder.** GitHub serves a `.html` file in a
repository as source rather than rendering it, so the link cannot show the page
until it is published somewhere that does. Embedding the page as a resource and
opening a temporary copy would need no hosting at all, and is the likely fix.

**The word "Stop" is back in the recording pill.** Removing it in 1.6.1
confused *what* with *how*: the green frame says a recording is running, but
only the word says this small box is the thing that ends it. A hand cursor and
a hover border are discoverable by accident, which is not the same as being
discoverable.

**The version in the menu's title row no longer has a `v`** —
`SnipTextProUltra · 1.6.2 · markpelayo`.

**Show Saved Files** sits directly above *Sanitize and Restore Default* again,
below *Screen Recording Settings* — settings together, then the two rows that
reach outside the app.

**The executable carries its version: `SnipTextProUltra_1.6.2.exe`.**

The name is read from the `VERSION` file by CMake, by `build.bat` and by CI
rather than written into each, so it cannot drift from what the binary reports
about itself, and bumping `VERSION` renames the output with nothing else to
remember. A literal in the CI script would have silently stopped matching on
the next bump, and "no binary found" on a release run is a poor way to discover
that. The resource block's `OriginalFilename` follows the same value, and
`release.sh` now checks it.

## Verification

The text normaliser's tests pass. Everything else here is verified by use.

The menu-in-capture fix is the one worth confirming deliberately, because it
only ever reproduced on slower hardware: take a *ScreenshotToText a Region* on
the machine where it used to happen and check the menu row is absent from the
result.

`build.bat` reads `VERSION` with new batch-file code, where quoting is easy to
get wrong — worth confirming it produces `SnipTextProUltra_1.6.2.exe` rather
than something with a stray quote in it.
