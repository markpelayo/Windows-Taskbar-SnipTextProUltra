# v1.7.1

One fix, and the reason the last two attempts at it failed.

## The menu appearing in captures

The cause has been known since 1.6.3: Windows has a visual effect —
**Performance Options → Visual Effects → "Fade out menu items after
clicking"**, on by default — that keeps the menu *window* alive and fading for
roughly 200 ms after the click. `TrackPopupMenuEx` has returned and the `HMENU`
is destroyed, but the row that was clicked is still on screen, so it lands in
the capture. For *ScreenshotToText* it is then recognised and copied along with
what the user actually wanted.

1.6.3 added the right mechanism and then disabled it with one line. The
suppressor walked this thread's popup-menu windows and took them out of the
capture, but it began with:

```cpp
if (!::IsWindowVisible(hwnd)) return TRUE;   // skip windows not on screen
```

That guard was added as an obvious optimisation. During a fade-out it is
exactly wrong: Windows hides the menu window and lets DWM dissolve the surface
it last rendered, so the window that needed excluding reported itself
invisible and was skipped. Three layers of defence, switched off by the first
line of the function.

The guard is gone. Beyond it, the capture path now does this, cheapest first:

| | | |
|---|---|---|
| 1 | Exclude or hide any menu window of ours | free, instant |
| 2 | **Only if** `SPI_GETMENUFADE` reports the effect on: wait for that window to disappear | returns the instant it does, ≤300 ms |
| 3 | Settle for the DWM animation of the last rendered frame | 30 ms |

A machine with the effect switched off never reaches steps 2 or 3 — it pays
one window enumeration and nothing more.

Step 3 is a delay, which is the shape of fix this deliberately avoided twice.
It is here because if the menu window is already *destroyed* and what remains
is DWM dissolving its last frame, there is no window to exclude or hide and
nothing else can work. It is bounded, small, and gated on the setting that
causes the problem.

The wait in step 2 pumps paint and sent messages rather than sleeping, because
the menu window belongs to this thread — if USER32 drives the fade from a timer
here, sleeping would stall the animation being waited on and the ceiling would
always be reached.

`TPM_NOANIMATION` has been passed when raising the menu since 1.6.3. It is
per-call and changes no system setting, but it is documented as affecting how a
menu is *displayed*, and it is not the fix on its own.

## Verification

Test with **"Fade out menu items after clicking" left ON** — switching it off
would hide whether this works. Take a *ScreenshotToText a Region* and confirm
the menu row is absent from the result.

If it still appears, that is informative rather than baffling: it means the
window is destroyed before the enumeration can see it and 30 ms is not enough
for the dissolve. The next step would be a build that reports how many menu
windows it actually found, since there is no log to consult.
