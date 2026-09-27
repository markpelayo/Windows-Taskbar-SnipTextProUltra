# v1.7.2

The flyout menu no longer appears in captures. Fourth attempt, and the first
one based on a measurement instead of a theory.

## What was actually happening

Windows has a visual effect — **Performance Options → Visual Effects → "Fade
out menu items after clicking"**, on by default — that leaves the menu visible
for roughly 200 ms after the click. `TrackPopupMenuEx` has returned and the
`HMENU` is destroyed, but the row that was clicked is still on screen, so it
lands in the screenshot. For *ScreenshotToText* it is then recognised and
copied along with what you actually wanted.

Three attempts failed before this one, each for a reason that looked sound at
the time. A diagnostic build settled it. It reported:

```
Fade effect enabled:        yes
Menu windows found:         0
Wait loop iterations:       0
Time spent waiting:         0 ms
```

**There is no window.** By the time a command runs, Windows has already
destroyed the menu window, and what remains on screen is DWM dissolving the
surface it last rendered — a ghost with no handle. That explains every
previous failure at once:

| Attempt | Why it could never work |
|---|---|
| `WDA_EXCLUDEFROMCAPTURE` | nothing to apply it to |
| `ShowWindow(SW_HIDE)` | nothing to hide |
| Wait for the window to go | it already had; the loop exited instantly |
| `TPM_NOANIMATION` | does not govern the dissolve |
| `DwmFlush` | did exactly what it promises, and returned a frame containing a half-faded menu |

## The fix

A wait is the only mechanism left, so the work went into making it cost as
little as possible and as rarely as possible. It is **250 ms**, and it applies
only when **both** of these are true:

- **Windows reports the fade effect as on.** Switch that checkbox off and
  nothing waits — which is also why turning it off appeared to "fix" the bug.
- **A menu started the capture.** `Ctrl+Shift+1` through `Ctrl+Shift+6` never
  show a menu, so the shortcut path is exactly as fast as it has always been.
  That is the path anyone using this regularly actually takes.

So the cost falls on a menu-initiated capture, on a default Windows install,
where you have just spent a second reading a menu — and nowhere else.

It is a plain sleep rather than the message pump the previous attempt used.
That pump existed on the theory that USER32 drove the fade from a timer on our
own thread; with no window of ours involved, DWM animates in its own process
and our thread sleeping cannot stall it.

## On the fixed 250 ms

It is unsatisfying and it is deliberate. The adaptive alternative — sample the
rectangle the menu occupied and poll until two grabs match — returns sooner in
the common case, but stops early if two consecutive samples happen to agree
mid-dissolve, which puts the menu back in the capture.

After three clever fixes that each failed for a different subtle reason, a
number that can be reasoned about completely is worth more than one that is
usually faster. If 250 ms turns out to feel sluggish in use, that is the point
at which the polling is worth adding — with evidence rather than in
anticipation.

## Verification

Test with **"Fade out menu items after clicking" left ON**. Switching it off
would only demonstrate the workaround.

- *ScreenshotToText a Region* from the menu: the menu row should be absent from
  the capture, and absent from the recognised text.
- The same via `Ctrl+Shift+3`: should be as immediate as before, with no added
  delay.
