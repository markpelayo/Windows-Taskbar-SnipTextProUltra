# SnipText Pro Ultra for Windows — v1.0.0

A pinnable Windows taskbar utility that does three things from one icon:
**screenshot with an annotation editor**, **screenshot to text** via on-device
OCR, and **screen recording** to MP4.

It is a C++/Win32 port of
[MacOS-menubar-SnipText-ProUltra](https://github.com/markpelayo/MacOS-menubar-SnipText-ProUltra),
which does the same three things from the macOS menu bar.

---

## ⚠️ Read this before you install

**This release has never been compiled or run on a Windows machine.**

It is a complete, carefully reviewed implementation written from the macOS
source — not a verified one. Two full static review passes fixed around thirty
real defects before tagging, but the first real build will almost certainly
surface more. Treat v1.0.0 as pre-release whatever the version number says.

The areas most likely to need work on first contact:

- **Media Foundation encoding.** The sink-writer configuration, the RGB32
  input stride and the AAC audio path are all written from documentation.
  Expect the recorder to be where the first bugs are.
- **The WinRT OCR plumbing.** It uses the ABI headers and WRL rather than
  C++/WinRT, to keep the build dependency-free. The `SoftwareBitmap` buffer
  handling is the fiddly part.
- **Multi-monitor and mixed-DPI setups.** The program is Per-Monitor-V2 aware
  and works in physical pixels throughout, which is the correct design — but
  "correct design" and "correct on your three-monitor desk" are different
  claims.

What *is* verified: the geometry-driven text normaliser, against the twenty
assertions in `tests/TextNormalizerTests.cpp`. Those expectations were checked
against an independent implementation of the same algorithm before being
written down.

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

### How it behaves as a Windows app

macOS has a menu bar; Windows does not. Pinning on Windows pins a *shortcut*,
so "clicking the icon" means launching the exe. The first launch stays resident
to hold the hotkeys; every later launch finds the running instance, tells it to
open its menu, and exits. One icon, one menu.

While idle there is no tray icon and no window — no timer, no thread, nothing
running. A tray icon appears only while recording, as a one-click Stop.

---

## Shortcuts

| | |
|---|---|
| `Win+Alt+1` | Screenshot — region |
| `Win+Alt+2` | Screenshot — full screen |
| `Win+Alt+3` | Screenshot to Text — region |
| `Win+Alt+4` | Screenshot to Text — full screen |
| `Win+Alt+5` | Record — region (press again to stop) |
| `Win+Alt+6` | Record — full screen (press again to stop) |

**Known collision:** the Windows shell already uses `Win+Alt+<digit>` to open
the Jump List of the pinned taskbar app in that position, and it registers
first. Some or all of these six may fail to register, in which case that
shortcut does nothing for the session and the log names it — the menu item
always works. `Ctrl+Alt` and `Alt+Shift` are both unclaimed and are a one-line
change to the modifier mask in `RegisterHotkeys()`.

---

## Known issues

- `Win+Alt+<digit>` may be taken by the shell, as above.
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
