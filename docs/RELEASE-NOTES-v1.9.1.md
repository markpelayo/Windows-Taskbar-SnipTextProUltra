# v1.9.1

A fix for a regression I introduced in 1.9.0 that broke every drawing tool.

## What was broken

Nothing could be drawn. Arrow, rectangle, ellipse, line, pen, lift — every
completed drag was silently discarded, and Crop drew its dashed marquee and
then did not crop.

The preview *during* the drag was correct, which is what made it look so
strange: the tool worked right up to the instant you let go, and then the
mark vanished.

## Why

1.9.0 added a `WM_CAPTURECHANGED` handler to the canvas. The intent was
sound — a drag interrupted by Alt-Tab, a lock screen or a UAC prompt would
otherwise leave the mark following the pointer around afterwards, because
mouse-up never arrives. The handler resets the drag state:

```cpp
case WM_CAPTURECHANGED:
    dragMode_ = DragMode::None;
    ...
```

And `WM_LBUTTONUP` began like this:

```cpp
case WM_LBUTTONUP: {
    ::ReleaseCapture();
    ...
    if (dragMode_ != DragMode::Drawing) {   // "a move just ended, nothing to commit"
        return 0;
    }
```

**`ReleaseCapture` sends `WM_CAPTURECHANGED` synchronously.** Not posts —
sends. So the sequence on every single mouse-up was:

1. `WM_LBUTTONUP` starts
2. `ReleaseCapture()` → `WM_CAPTURECHANGED` runs → `dragMode_ = None`
3. control returns to `WM_LBUTTONUP`
4. `dragMode_` is no longer `Drawing`, so it takes the "completed move"
   branch and returns without committing anything

The state the handler needed had been overwritten by a message its own first
line provoked.

## The fix

Read the mode into a local **before** releasing the capture, and use the
local for the rest of the handler:

```cpp
const DragMode ending = dragMode_;
::ReleaseCapture();
...
if (ending != DragMode::Drawing) { ... }
```

A reset arriving from inside `ReleaseCapture` can then no longer cancel a
drag that is in the middle of being committed — and the handler still does
its job when the capture is genuinely taken away.

It now also clears the in-progress draft, which is what it should have done
for a real interruption from the start. That is safe on the ordinary path
because mouse-up moves the draft out unconditionally rather than checking
whether one exists.

## The lesson worth recording

A Win32 call inside a message handler can send messages back into the same
window before it returns. `ReleaseCapture`, `SetCapture`, `DestroyWindow`,
`SetFocus`, `SetWindowText` and `ShowWindow` all do. Any handler that both
calls one of those and reads its own state afterwards has to take the state
first.

The review over 1.9.0 flagged the *absence* of a `WM_CAPTURECHANGED` handler
as a pre-existing gap. I added one and created this, after the review had
finished.

**And the repository already knew.** `RegionOverlay` has had a
`WM_CAPTURECHANGED` handler since the overlay was written, guarded by a
`releasingCapture_` flag, with this comment above it:

> The guard is essential: ReleaseCapture sends this message back
> synchronously even when we are the ones releasing, so without it
> OnMouseUp would find its own state already wiped and every click would
> do nothing.

That is this bug, described in advance, nine hundred lines away in the same
repo. The two handlers now cross-reference each other, because having the
answer written down somewhere is not the same as finding it.

## Verification

1. Draw one of each: arrow, rectangle, filled rectangle, ellipse, line, pen
   stroke, lift. All seven should stay on the canvas.
2. Crop. It should actually crop.
3. Move a mark, resize one by a handle, swing a label. All three should
   still work, and each should be one undo step.
4. Start a drag, then Alt-Tab away mid-drag and come back. The mark should
   not be following the pointer, and no half-finished draft should be left
   on the canvas.
