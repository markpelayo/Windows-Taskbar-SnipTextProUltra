# SnipText Pro Ultra for Windows — v1.1.0

A pinnable Windows taskbar utility that does three things from one icon:
**screenshot with an annotation editor**, **screenshot to text** via on-device
OCR, and **screen recording** to MP4.

It is a C++/Win32 port of
[MacOS-menubar-SnipText-ProUltra](https://github.com/markpelayo/MacOS-menubar-SnipText-ProUltra),
which does the same three things from the macOS menu bar.

This is the first release with a downloadable executable, and the first one
shaped by actually using the thing rather than only reading the code.

---

## Download

`SnipTextProUltra.exe` is attached below. One file, no installer, nothing to
put beside it — the binary is statically linked.

**Windows will warn you about it.** The executable is not code-signed, so
SmartScreen shows *"Windows protected your PC"*. That warning is correct: it
means Windows cannot tell who published this, and the honest answer is that
it was published by one person with no certificate. If you choose to run it
anyway, click **More info → Run anyway**.

If you would rather not click through a SmartScreen warning — a reasonable
position, and the habit is worth protecting — [build it from
source](../README.md#build) instead. `build.bat run` is the whole process, and
it is also the only way to be certain the binary matches the code you can
read.

A `SHA-256` checksum is attached beside the executable. It is not a signature
and does not pretend to be one; it only lets you confirm the file you
downloaded is the file this build produced:

```
Get-FileHash .\SnipTextProUltra.exe -Algorithm SHA256
```

**Requirements:** Windows 11, or Windows 10 version 1903 or later, 64-bit,
with an OCR language installed (Windows ships one with your display language).

---

## What's new in 1.1.0

### Configurable shortcuts

**Settings → Shortcuts** lists all six commands with their current bindings.
Pick one, a small window appears, press the combination you want, Enter to
save.

A bare function key works — press `F9` and that is the binding, no modifier
required. **Delete** unbinds a shortcut entirely, leaving the action reachable
only from the menu. **Esc** cancels, and **Reset to Defaults** restores all
six.

Two things it does quietly. The capture window **unregisters the app's own
hotkeys while it is open**, because a global hotkey fires before the
foreground window sees the key — so otherwise pressing the shortcut you were
trying to change would trigger its action instead of being captured. And
binding a combination that another action already owns **takes it away from
that action**, rather than silently failing to register and leaving you with a
shortcut that does nothing.

Bare letters are refused. Registering a plain `A` would swallow that key in
every program on the machine, including the window you would need to type in
to undo it.

### Default shortcuts are now Ctrl+Shift

| | |
|---|---|
| `Ctrl+Shift+1` | Screenshot — region |
| `Ctrl+Shift+2` | Screenshot — full screen |
| `Ctrl+Shift+3` | Screenshot to Text — region |
| `Ctrl+Shift+4` | Screenshot to Text — full screen |
| `Ctrl+Shift+5` | Record — region (press again to stop) |
| `Ctrl+Shift+6` | Record — full screen (press again to stop) |

### A menu that says what it does

The headers over the capture sections are gone. The command names carry them now —
*Screenshot a Region…*, *ScreenshotToText a Region…*, *Record Region…* — so a
header would only repeat the row beneath it. The *Startup* header went the
same way, since the row under it already began with "Run at Startup".

The three scattered *Show Saved* rows are now one **Show Saved Files**
submenu, just above *Sanitize and Restore Default*. They are the same kind of
thing, and collecting them leaves each capture section as nothing but its
commands. The parent row greys out when all three folders are empty.

The first row names the program, its version and its author, and clicking it
opens the repository.

### A new icon

Material style: a gradient tile with anti-aliased corners, viewfinder brackets
and three text lines at 24 px and above, two bold bars below that where the
brackets would turn to mud. The previous one was dark ink on transparency and
disappeared into a dark taskbar.

### Fixes

- **The Record button could not be clicked.** It sits inside the selection
  rectangle and nothing hit-tested it, so the click aimed at it was read as
  "start dragging the selection".
- **The pointer did not change over the Record button.** It now shows a hand,
  checked in the same order the click handler checks it, so the pointer and
  the click agree.
- **The hint line could be clipped off the bottom of the screen**, and could
  be drawn straight across the Record button on a short selection.

---

## What has and has not been tested

### Verified

- **It builds**, clean, under `/W4 /WX` with MSVC — not a single compiler
  warning in the tree — and CI does it on every push.
- **It runs.** On Windows 11, on one machine, at one resolution. The menu
  opens, the region overlay works, the recorder produces files.
- **The text normaliser**, against twenty assertions whose expectations were
  checked against an independent implementation of the same algorithm.

### Not verified

In rough order of how likely it is to bite:

- **Video output quality.** Recordings are produced, but nobody has watched
  one frame by frame across frame rates, quality settings, or with audio on.
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
launch writes an environment block to
`%LOCALAPPDATA%\SnipText\Logs\SnipText.log`. If something misbehaves, that log
is what explains it — and it is the right thing to attach to a bug report.

---

## Known issues

- On a full-screen recording the Stop pill has nowhere outside the captured
  area to sit, so it appears in the video. The log says so when it happens.
- Full-screen capture covers one monitor, the one under the pointer.
- Table columns merge into a single line; *Keep Line Breaks* preserves the
  rows.
- No webcam recording, and no system-audio (loopback) recording. Microphone
  input works.
- Not code-signed.

---

## Documentation

- [README](../README.md) — what it does and how to use it
- [Architecture](ARCHITECTURE.md) — how it works, the interesting problems,
  and the bugs worth knowing about
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
