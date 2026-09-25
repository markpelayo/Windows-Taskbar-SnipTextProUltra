# SnipText Pro Ultra for Windows — v1.3.1

A pinnable Windows taskbar utility that does three things from one icon:
**screenshot with an annotation editor**, **screenshot to text** via on-device
OCR, and **screen recording** to MP4.

This release is about knowing the program is there, and about the sound it
makes when it works.

It also carries everything intended for **v1.2.0** and **v1.3.0**. Both of
those tags were created before their commits existed, so both pointed at the
same older commit — their binaries reported 1.1.0 and their release bodies
fell back to the whole changelog. Neither shipped what it claimed to. This is
the first release to actually contain any of it.

---

## Download

`SnipTextProUltra.exe` is attached below. One file, no installer, nothing to
put beside it — the binary is statically linked.

**Windows will warn you about it.** The executable is not code-signed, so
SmartScreen shows *"Windows protected your PC"*. That warning is correct: it
means Windows cannot tell who published this, and the honest answer is that it
was published by one person with no certificate. If you choose to run it
anyway, click **More info → Run anyway**.

If you would rather not click through a SmartScreen warning — a reasonable
position, and the habit is worth protecting — [build it from
source](../README.md#build) instead. `build.bat run` is the whole process, and
it is the only way to be certain the binary matches the code you can read.

A `SHA-256` checksum is attached beside the executable. It is not a signature
and does not pretend to be one; it only lets you confirm the file you
downloaded is the file this build produced:

```
Get-FileHash .\SnipTextProUltra.exe -Algorithm SHA256
```

**Requirements:** Windows 11, or Windows 10 version 1903 or later, 64-bit,
with an OCR language installed (Windows ships one with your display language).

---

## What's new

### A tray icon, permanently

There is now an icon in the notification area whenever the program is running.
Either mouse button opens the menu.

This reverses an earlier decision. The original design put nothing in the tray
while idle, on the grounds that an idle utility should be invisible — but in
practice that meant Task Manager was the only way to confirm the program was
alive, and the pinned taskbar icon was the only way to reach Quit. An icon
that says "this is running, here is its menu" is worth the one shell call it
costs. There is still no timer and no background thread while idle.

While recording, that same icon alternates with a red square once a second
rather than a second icon appearing, so the tray slot never moves.

### A shutter that sounds like a shutter

The old capture sound was a Windows system alias — the "you can't click that"
ding, which is exactly the wrong message for a capture that worked.

The new one is synthesised at startup: two transients about 70 ms apart, each
a short filtered noise burst with a fast-decaying low tone behind it, which is
roughly what a mirror going up and blades closing actually sound like.
Generating it rather than bundling a `.wav` keeps the executable a single
self-contained file.

**Shutter Sound** is now a submenu:

```
  Off
  Built-in Shutter          ✓
  Custom Sound…
  ──────
  Preview
```

*Custom Sound…* takes any `.wav`. If that file later goes missing the built-in
sound is used instead, so a moved file means a different sound rather than
silence — and the log says so, once.

### A narrower menu

```
SnipTextProUltra  ·  v1.3.0  ·  by markpelayo
──────────────────────
  Screenshot a Region…            Ctrl+Shift+1
  Screenshot Full Screen          Ctrl+Shift+2
──────────────────────
  ScreenshotToText a Region…      Ctrl+Shift+3
  ScreenshotToText Full Screen    Ctrl+Shift+4
  Copy: "Work Order #…"
──────────────────────
  Record Region…                  Ctrl+Shift+5
  Record Full Screen              Ctrl+Shift+6
──────────────────────
  Shortcuts              ▸
  Keep Line Breaks
  Shutter Sound          ✓ ▸
  Auto-Save Images
  Save Locations         ▸
  Video Settings         ▸
  Show Saved Files       ▸
  Sanitize and Restore Default…
──────────────────────
✓ Run at Startup: 15 s   ▸
──────────────────────
  Quit SnipTextProUltra
```

A Win32 menu is exactly as wide as its longest row, and every other row
stretches to match. Two rows were doing that:

- **The title row.** It now reads `SnipTextProUltra · v1.3.0 · by markpelayo`.
  The repository's `Windows-Taskbar-` prefix says which platform it targets,
  which the program running on that platform does not need to be told.
- **The three folder rows**, each able to grow a `": FolderName"` suffix.
  They are now one **Save Locations** submenu, sitting above *Show Saved
  Files* — one says where captures go, the other opens what went there.

The *Settings* and *Startup* headers are also gone; the rows beneath them said
the same thing.

One caveat: the whitespace after "by markpelayo" is the accelerator column.
Windows reserves it on **every** row as soon as **any** row uses one, and
there is no way to opt a single row out short of owner-drawing the whole menu
— which would cost native theming, high-contrast mode and screen-reader
support. Not a good trade for a utility menu.

### Also in here, from the withdrawn 1.2.0

- **Clearer command names**: *Screenshot a Region…* and *ScreenshotToText a
  Region…* read as instructions rather than labels.
- **The capture-section headers are gone**, since the command names carry
  them.
- **Show Saved Files** collects the three output folders into one submenu,
  greyed out when all three are empty.

### Renamed to SnipTextProUltra

The tray tooltip, the Quit row, the window title and the executable's version
resource all agree now, so Task Manager and the tray call it the same thing.

---

## What has and has not been tested

### Verified

- **It builds**, clean, under `/W4 /WX` with MSVC — not a single compiler
  warning in the tree — and CI does it on every push.
- **It runs.** On Windows 11, on one machine, at one resolution.
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
