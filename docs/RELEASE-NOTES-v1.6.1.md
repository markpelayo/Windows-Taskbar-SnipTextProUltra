# v1.6.1

Two fixes to things v1.6.0 shipped, and the recording indicator finishing the
job it started.

## Fixed

**The mouse pointer strobed on screen for the whole of a recording.**

The recorder's per-frame `BitBlt` passed `CAPTUREBLT`, which tells GDI to
include layered windows — and to do that, the system takes the cursor down and
puts it back around the blt. Once, for a screenshot, that is imperceptible,
which is why the screenshot path still uses it. Thirty times a second it is a
visible flicker, for as long as the recording runs.

The give-away was that the recorded frames were always fine. The flicker
existed only on the real desktop and never in the file, which is what made it
look like a display driver problem rather than this program's.

What dropping the flag costs is layered-window fidelity in recordings. In
practice DWM composites most of what matters into the screen DC anyway, and a
recording missing a translucent overlay is a far smaller problem than a pointer
that flashes throughout it.

**The Lift tool's button was missing from the editor on small captures.**

The minimum window size was raised when Lift was added in 1.6.0 — but only for
*resizing*. The window creation path had its own copy of the old number, so any
capture small enough to hit the floor opened one button short, and Lift was the
last button.

Both now come from one derived constant, so the size the window opens at and
the size it can be dragged to cannot disagree. The client-to-frame conversion is
also DPI-aware now, which it was not: `AdjustWindowRectEx` reports non-client
metrics at 96 DPI regardless of the display, so on a scaled monitor the client
area could still be squeezed under the minimum.

## Added

**Full-screen recordings get the green dashed frame.**

A region covering the whole monitor has no outside to put a border in, so the
frame was created, positioned off the edge of the desktop, and never seen —
leaving the corner pill as the only sign a recording was running.

For that case the frame is now drawn just inside the screen edges and hidden
from the capture with `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`, the
mechanism Windows provides for exactly this; its documentation gives "windows
that show video recording controls" as the example. It needs **Windows 10
version 2004 or later**. On anything older the frame stays outside, which means
full screen has no frame, exactly as before.

Two guards, because a border wrongly believed to be hidden would be recorded
into every video:

- The test is whether the region covers **all four** monitor edges, not any of
  them. "Any" would catch every region merely snapped to a screen edge and put
  dashes over the recorded content on all four sides.
- The affinity is **read back** and required to equal `WDA_EXCLUDEFROMCAPTURE`
  exactly. That flag is `0x11`, which is `WDA_MONITOR` (`0x01`) plus a bit, so
  a build predating 2004 could plausibly accept the call and apply
  `WDA_MONITOR` instead — putting a black band in the video rather than
  nothing.

The same mechanism keeps the **Stop pill** out of the video when it has to sit
inside the recorded area. Until now that was an accepted limitation, noted in
the log and visible in the file.

**A red dot beside "Stop Recording" in the menu**, so opening the menu says a
recording is running at a glance.

It is a bitmap in `MENUITEMINFO::hbmpItem` rather than a `●` in the label,
because a menu draws its text in the system colour — a typed dot comes out
black, or white in dark mode, which is the one thing a recording dot must not
be. `hbmpItem` puts it in the check-mark gutter with the system's own spacing,
and stays right after a theme change. Sized from `SM_CXMENUCHECK`, drawn with
premultiplied alpha.

## Changed

**Confirmations last 3 seconds instead of 1.6, and say what happened.**

These are the only feedback the program gives, there being no notification
banner anywhere in it, and 1.6 seconds was long enough to notice a message but
not to read one.

| was | is |
|---|---|
| `reset` | `Sanitized and restored to defaults` |
| `98 chars copied` | `Copied 98 characters to the clipboard` |
| `copied` | `Copied 98 characters again` |
| `no text found` | `No text found in that capture` |
| `copy failed` | `Found the text, but the clipboard refused it` |
| `saved …mp4` | `Recording saved · …mp4` |
| `⚠ capture failed` | `⚠ The capture failed` |

The box also clamps its width to the screen and ellipsises the end rather than
the middle, since the longest message carries a generated file name.

**The Stop pill is about a third narrower** — `● 00:06` rather than
`● 00:06   Stop`. Once the frame appeared on full-screen recordings, the frame
says a recording is running and the pill no longer has to spell out what it is
for; and on full screen it sits over the work for the whole take. Still one
click to stop, with the hand cursor and the red hover border saying so.

Removing it entirely on full screen was considered and rejected. The frame and
the pill answer different questions — *what* is being recorded versus *how
long* — and the pill is the only always-visible way to stop. Falling back to
the tray icon would reintroduce the exact problem it was built for, since
Windows 11 hides the notification area behind a chevron by default.

## Verification

The text normaliser's tests pass. Everything else here is verified by use
rather than by test.

Two things depend on the machine rather than the code:

- The full-screen frame needs Windows 10 2004 for `WDA_EXCLUDEFROMCAPTURE`. On
  an older build you simply get no frame on full screen. Either way the first
  full-screen recording should have **no green border in the file** — if it
  does, the affinity check passed but the flag did not take.
- Dropping `CAPTUREBLT` trades layered-window fidelity for a steady cursor. If
  something translucent you expected to record is missing, that is why.
