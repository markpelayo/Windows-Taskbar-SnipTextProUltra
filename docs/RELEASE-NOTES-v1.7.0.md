# v1.7.0

An audit release. No new features — eleven defects fixed, the idle memory
footprint roughly halved, and the program's peak reduced by the largest single
allocation it made.

Three independent audits were run over the codebase: one for resource and
handle leaks, one for memory footprint and dead code, and one for correctness.
The leak audit found **no per-frame or per-capture leak anywhere** — the RAII
layer is used consistently — so what follows is everything else they turned up.

## The two that were affecting recordings already

**Recordings played back faster than real time on any machine that could not
sustain the frame rate.** The loop paced against the clock but stamped every
frame with a fixed interval, so when it fell behind it produced fewer frames
than the file claimed. A 60-second take could come out as a 22-second file,
with the audio running progressively ahead. Timestamps now come from
`QueryPerformanceCounter` and each sample's duration is the measured gap.

**Audio slid ahead of the picture after any quiet stretch.** WASAPI marks a
packet silent when the microphone has nothing to give, which is normal for an
idle mic, and those packets consume real time. The clock only advanced for
packets that were written — so staying quiet for twenty seconds put everything
said afterwards twenty seconds early.

Neither would have been obvious on a fast machine recording a small region.
Both would be plainly wrong on a slower one, or at 4K.

## Memory

| | Before | After |
|---|---|---|
| Idle, app-attributable | ~3.5–8 MB | **~1.5–3 MB** |
| Peak, record a 4K region | ~100 MB+ | **~33–66 MB lower** |

**Media Foundation now starts with the first recording** rather than at launch.
`MFStartup` commits roughly 2–5 MB and spins up its own worker threads, and for
a utility that mostly sits idle — and that many people will never record with
at all — that was about half the idle footprint for nothing. The cost is tens
of milliseconds before the first frame of the first recording.

**The frozen desktop is released before the recorder allocates.** Recording a
region keeps the overlay alive while the recorder starts, and the overlay holds
the whole virtual desktop at 32bpp: 33 MB per 4K monitor, 66 MB for two. It was
resident while the recorder allocated its own buffers and the encoder spun up.

Everything this program allocates itself, at idle, now comes to **under
100 KB**. The rest is GDI+, which every drawing path uses.

## Also fixed

- Two paint handlers could spin a window at 100% CPU forever if `BeginPaint`
  failed — on the region overlay, a screen-covering window that Esc cannot
  reach.
- The Audio menu could select the wrong microphone if a device arrived while
  the submenu was open.
- Save as PNG could flash "Saved" over a truncated file.
- A late-finishing recording could be reported under the wrong file name.
- Two failed-recording paths left a zero-byte `.mp4` in the Videos folder.
- A crash on exit was possible when a worker was abandoned still holding Media
  Foundation objects.
- The failure dialog could be used to start a new capture from inside its own
  message loop.
- Three places read pixel bytes without flushing GDI first — one of them the
  recorder, on every frame at any Quality below the top one.

## Also

**Another attempt at the menu appearing in captures.** The reason the 1.6.3
attempt failed is now known, and it was mine: the suppressor skipped any menu
window that reported itself invisible, which during a fade-out is exactly what
the window does. Windows hides it and DWM dissolves the surface it last
rendered, so a guard added as an obvious optimisation skipped the only case
that mattered.

That guard is gone. Beyond it, if and only if Windows reports the fade effect
as enabled, the capture waits for the menu window to disappear and then settles
briefly for the DWM animation — bounded, and never reached on a machine with
the effect switched off. `TPM_NOANIMATION` has been passed since 1.6.3 and is
not the fix by itself.


**Text Layout → Compare the Two on Sample Text opens a page that renders.**
The sample page is now Markdown as well as HTML, and GitHub renders Markdown at
its own URL — so the menu row opens readable examples in the browser rather
than the markup GitHub serves for a `.html` file. That link was a placeholder
in 1.6.3 for exactly that reason, and closing it needed no hosting at all.

## Leaner

Thirteen unreachable functions, fields and accessors deleted, most orphaned
when the log was removed in 1.6.3. Each was verified to have no caller anywhere
in `src/` or `tests/`. No behaviour change.

The editor now moves a completed pen stroke instead of copying it — a long
scribble carries every sampled point, hundreds of kilobytes, once per stroke.

## Looked at and deliberately left alone

- **The undo stack** is capped at 50 snapshots, not by size, so a few long pen
  strokes can make it large. Capping by point count would make undo depth vary
  silently with what you had drawn, which is worse than the memory.
- **The encoder's input queue** is unbounded because throttling is disabled on
  purpose; re-enabling it would change frame pacing.
- **The shutter sound buffers** are never freed — 17 KB, and `PlaySound` reads
  them from its own thread past static destruction. Freeing them is a
  correctness regression.
- **GDI+** stays initialised at launch; unlike Media Foundation, every drawing
  path uses it.
- **The overlay and editor repaint buffers** are allocated per paint and freed
  immediately. Caching them would trade churn for a permanently held
  multi-megabyte bitmap — worse for the number that matters.

## Verification

The text normaliser's tests pass. Everything else is verified by use, and none
of this has been through a compiler on the author's machine.

The recorder changes are the ones to check deliberately, because they are the
ones a fast machine hides:

- Record for a measured 60 seconds and confirm the file is 60 seconds long and
  plays at the right speed.
- Record with a microphone, stay silent for ~20 seconds, then speak, and
  confirm the speech lands where it happened.
- Record at a Quality below the top one, which is the path where the missing
  GDI flush was.
- Confirm the first recording after launch still starts, since Media Foundation
  now initialises at that moment rather than earlier.
