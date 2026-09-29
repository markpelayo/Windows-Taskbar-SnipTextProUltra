# v1.7.7

One fix, and the last of the 1.7 line. This is the build to keep as a known-good
reference before the 1.8 interface changes.

## What was happening

Select **Lift**, then click the colour swatch. The picker opened over the start
of the hint, so *Drag to copy a piece · Shift-drag to cut it out* lost its first
few words — the sentence explaining the one tool in the editor with a modifier.

## Why

Two things anchored to the same corner, and neither knew about the other.

The colour picker is 204 × 90 and opens directly above the swatch. The swatch is
the leftmost control on the bottom bar, so the picker rises into the
**bottom-left of the canvas**.

The Lift hint was centred horizontally, 12px above the canvas bottom — the
**same vertical band**.

At the 700px minimum window width:

```
picker   x  10 .. 214
hint     x 207 .. 492      <-- 7px of overlap
```

Seven pixels does not sound like much, but the picker is a topmost window rather
than something painted on the canvas, so it is drawn over the hint rather than
under it, and the overlap reads as a clipped sentence.

It got worse as the window got narrower, and better as it got wider, which is
exactly the kind of bug that survives testing: at a comfortable window size the
two are merely adjacent.

## The fix

The hint is **right-aligned** now instead of centred:

```cpp
const REAL boxLeft = (std::max)(0.0f, static_cast<REAL>(width) - boxWidth - 12.0f);
```

The point is not that right happens to be free today. It is that the picker is
anchored to the **left** edge and the hint is now anchored to the **right** one,
so the clearance is a property of the layout rather than a coincidence that held
at the window sizes anyone happened to try. At the minimum width the hint starts
at x 403 against a picker ending at 214 — 189px of daylight, and every pixel of
extra window width adds two more.

It also reads better: the hint now sits under the tool buttons, which is where
the click that summoned it happened.

## Verification

1. Pick **Lift**. The hint appears at the bottom **right** of the canvas.
2. With Lift still selected, click the colour swatch. The picker opens at the
   bottom left and the hint stays completely readable.
3. Drag the window down to its minimum width with both showing. They should stay
   clear of one another the whole way.
4. Start a Lift drag — the hint should disappear, as before, since by then the
   choice has been made.
