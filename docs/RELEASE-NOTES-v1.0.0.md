# SnipText Pro Ultra for Windows — v1.0.0

A pinnable Windows taskbar utility that does three things from one icon:
**screenshot with an annotation editor**, **screenshot to text** via on-device
OCR, and **screen recording** to MP4.

It is a C++/Win32 port of
[MacOS-menubar-SnipText-ProUltra](https://github.com/markpelayo/MacOS-menubar-SnipText-ProUltra),
which does the same three things from the macOS menu bar.

---

## Read this before you install

**It builds and it runs — on one machine.**

The build is clean under `/W4 /WX`, meaning not a single compiler warning in
the tree, and CI does it on every push. It has been launched and driven on
Windows 11 at one resolution: the menu opens, the region overlay works, the
recorder produces files. The text normaliser is covered by twenty assertions
whose expectations were checked against an independent implementation of the
same algorithm.

Everything past that is unverified. In rough order of how likely it is to
bite:

- **Video output quality.** Recordings are produced, but nobody has watched
  one frame by frame across frame rates, quality settings, or with audio on.
  The Media Foundation configuration was written from documentation.
- **Microphone audio.** The WASAPI capture and AAC path have not been
  exercised at all. They are written to fail soft — a bad microphone gives you
  a silent video, never a lost one — but that is a design claim, not a
  measurement.
- **Multi-monitor and mixed-DPI setups.** The program is Per-Monitor-V2 aware
  and works in physical pixels throughout, which is the correct design. The
  startup log prints your display layout; that is the first thing to check if
  a capture lands in the wrong place.
- **The annotation editor under sustained use.** Individual tools work; long
  sessions and deep undo stacks have not been hammered.

Verbose logging is **on by default** in 1.x for exactly this reason, and every
launch writes an environment block. If something misbehaves, that log is what
explains it.

---

## Installing

There is no binary download, on purpose. An unsigned executable from the
internet gets a SmartScreen warning that teaches people to click through
SmartScreen warnings, and building it yourself is the only way to be certain
the binary matches the source.

```
git clone https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra.git
cd Windows-Taskbar-SnipTextProUltra
build.bat run
```

`build.bat` finds Visual Studio on its own if you are not already in a
developer command prompt. Then right-click the taskbar button →
**Pin to taskbar**.

**Requirements:** Windows 11 (or Windows 10 1903+), Visual Studio 2022 with
the *Desktop development with C++* workload, and an installed OCR language
(Windows ships one with your display language).

---

## What's in it

### Screenshot → annotation editor

Arrow, rectangle, ellipse, line, freehand pen and text. Nothing is baked into
the image until you copy or save: every mark stays selectable, movable,
resizable and restylable, and undo covers all of that on the same footing as
drawing, because it stores whole-array snapshots rather than a list of added
shapes.

Annotations are held in image pixel coordinates, so the PNG you save is full
resolution and matches what you saw regardless of the window size.

### Screenshot to Text

On-device OCR via `Windows.Media.Ocr` — part of the OS, no model download, no
network, no third-party library.

The interesting part is what happens afterwards. OCR gives one entry per
visual line, which is wrong for prose and right for code. The decision is made
from **geometry, not character counts**: a line that stops well short of the
right margin didn't wrap, so its break is real. *Keep Line Breaks* switches the
whole geometry path off when you want exactly the lines the engine saw.

### Record Video

A resizable region or a whole screen, to H.264 MP4 via Media Foundation, with
optional microphone audio, 15/24/30/60 fps, three quality levels, and mouse
cursor and click capture.

The recorder is built so it cannot wedge. Every recording carries a generation
number that every asynchronous entry point checks, watchdogs sit on both the
start and stop paths, and the finish callback fires exactly once per
recording — never twice, never zero times.

While a recording runs, a **green dashed frame** marks the captured area and a
**`● 00:24  Stop` pill** carries the elapsed time and stops it in one click.
The frame is drawn strictly outside the captured rectangle, so it never ends up
in the video, and it is painted once and then costs nothing. The pill goes
outside the frame wherever there is room; on a full-screen recording there is
nowhere outside, so it sits in a corner and the log says it will be in the
video.

### How it behaves as a Windows app

macOS has a menu bar; Windows does not. Pinning on Windows pins a *shortcut*,
so "clicking the icon" means launching the exe. The first launch stays resident
to hold the hotkeys; every later launch finds the running instance, tells it to
open its menu, and exits. One icon, one menu.

While idle there is no tray icon and no window — no timer, no thread, nothing
running. The menu's first row names the program, its version and its author, so
a screenshot of it identifies the build.

---

## Shortcuts

| | |
|---|---|
| `Alt+Shift+1` | Screenshot — region |
| `Alt+Shift+2` | Screenshot — full screen |
| `Alt+Shift+3` | Screenshot to Text — region |
| `Alt+Shift+4` | Screenshot to Text — full screen |
| `Alt+Shift+5` | Record — region (press again to stop) |
| `Alt+Shift+6` | Record — full screen (press again to stop) |

`Alt+Shift` is unclaimed by Windows 11. It avoids `Win+Shift+S`, which is
the built-in Snipping Tool, and `Win+Alt+<digit>`, which the shell owns for
taskbar Jump Lists. If another program already has one of these, that
shortcut silently does nothing for the session and the log names it — the
menu item always works.

---

## Known issues

- Hotkeys are not configurable. Changing them is a one-line edit to the
  modifier mask in `RegisterHotkeys()`.
- Full-screen capture covers one monitor, the one under the pointer.
- Table columns merge into a single line; *Keep Line Breaks* preserves the rows.
- No webcam recording, and no system-audio (loopback) recording. Microphone
  input works.
- Not code-signed.

---

## Documentation

- [README](../README.md) — what it does and how to use it
- [Architecture](ARCHITECTURE.md) — how it works, the interesting problems, and
  the bugs worth knowing about
- [Changelog](../CHANGELOG.md)
- [Disclaimer](../DISCLAIMER.md) — no-warranty and liability terms, including
  what you are responsible for when you record a screen

---

## Licence and disclaimer

MIT © Mark Pelayo.

Two things deserve emphasis and are covered in full in
[DISCLAIMER.md](../DISCLAIMER.md):

**OCR is approximate.** Characters may be misread, omitted or invented, and an
error can be indistinguishable from correct output. Digits, identifiers,
amounts and dates are especially prone to it. Verify anything that matters
against the original.

**Recording may require consent.** A recording includes whatever is on screen
and whatever the selected microphone hears, and the app gives no notice to
anyone that it is happening. Complying with the law and with any workplace or
platform policy is your responsibility.
