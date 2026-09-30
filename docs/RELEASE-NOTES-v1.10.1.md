# SnipTextProUltra v1.10.1

**Fixes the feature 1.10.0 added.** It was drawing from freed memory: the
added screenshot came out solid black, and cropping over one crashed the app.

One root cause behind both.

---

## The bug

1.10.0 captured the region into a DIB, wrapped it in a `Gdiplus::Bitmap`, and
called `Clone()` on the wrapper — on the belief that `Clone` produces an
independent copy.

**It does not reliably. Cloning a bitmap that was constructed over an existing
buffer can hand back a bitmap still pointing at that buffer.**

The captured DIB was a local variable, so it was destroyed on the way out of
the capture function, and every later paint read a dangling pointer.

**Why black rather than an immediate crash**, which is the part worth getting
right: the DIB's storage is a *DIB section*, and deleting it unmaps the
address range. Reading unmapped memory faults straight away — so if that were
the whole story the first repaint would have crashed, not rendered black.

What actually happens is that the range gets **reused**. The very next canvas
paint allocates `scaledImage_`, a fresh DIB section of comparable size, which
the allocator plausibly satisfies from the range just released — and a fresh
DIB section is zero-filled. With `PixelFormat32bppRGB` the alpha byte is
ignored, so zeros are *opaque black* rather than transparent. Hence solid
black, deterministically.

Cropping is then the one operation that releases `scaledImage_` and
reallocates it at a **different** size. The old range is no longer reclaimed,
the dangling read lands on unmapped memory, and it faults. That is exactly why
cropping specifically crashed and nothing else did.

### Why the report's shape was the giveaway

Every detail in it pointed at the same thing, which is what made it findable
without a Windows machine:

- **An ordinary Lift was fine.** A lifted piece reads from the editor's own
  capture, which is alive for as long as the editor is.
- **Only an added shot was affected.** It was the only mark holding a pointer
  to something that had already been freed.
- **Cropping *away* from it worked.** Nothing made GDI+ read those pixels, so
  nothing faulted.
- **Lift + added shot crashed.** Not an interaction between them — the added
  shot alone was enough.

### The fix

The annotation now **owns the DIB outright**, and `Draw` builds the
`Gdiplus::Bitmap` view over it as a local when it paints. Constructing a view
over existing pixels copies nothing, and being a local it cannot outlive them.

That is exactly what `PictureForLift` already does for the capture on every
repaint, so it is the house rate rather than a new cost — and it removes the
*class* of bug rather than this instance of it. It is also one full-frame copy
cheaper than the version that was wrong.

The `shared_ptr` reasoning is unchanged and still load-bearing: an annotation
is copied wholesale into every undo snapshot, so the pixels are shared by
refcount rather than duplicated per step.

---

## Renamed to "Add a Screenshot"

The button tooltip and the *Change Keyboard Shortcut* row both say it now, and
so do the identifiers behind them, so the code and the interface use one name.

---

## The extra gaps are gone

The gap between **redo and copy**, and between **save and pin**, have been
removed. The right-hand run is now one tight group of five.

```
swatch slider │ 8 tools + Add a Screenshot │ Undo Redo Copy Save Pin
  how it looks│        make a mark         │     everything else
```

The bar reads as *changes the picture* / *does not*, and that is the only
distinction worth a gap when you are hunting for a button. Sub-dividing the
second half into history, output and window was more structure than there was
meaning.

It also cost width for nothing: the minimum client area drops from 760 × 424
to **740 × 424**.

---

## Upgrading

Nothing to do. If you added a screenshot in 1.10.0 and saved the result, the
black rectangle is in that PNG — the pixels were never there to save. Redo it
on this build.
