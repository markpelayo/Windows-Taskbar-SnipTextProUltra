# SnipText Pro Ultra for Windows — v1.5.0

A pinnable Windows taskbar utility that does three things from one icon:
**screenshot with an annotation editor**, **screenshot to text** via on-device
OCR, and **screen recording** to MP4.

This release fixes the thing that made *Screenshot to Text* unreliable for
codes, serial numbers and anything that isn't prose.

---

## Download

`SnipTextProUltra.exe` is attached below. One file, no installer, nothing to
put beside it.

**It is bigger now — about 15 MB rather than 1 MB.** That is the second OCR
engine and its trained model, both compiled in so the program stays a single
self-contained file with no DLLs and no network access.

**Windows will warn you about it.** The executable is not code-signed, so
SmartScreen shows *"Windows protected your PC"*. That warning is correct: it
means Windows cannot tell who published this. If you choose to run it anyway,
click **More info → Run anyway**. If you would rather not click through a
SmartScreen warning — a reasonable position — [build it from
source](../README.md#build) instead.

A `SHA-256` checksum is attached beside the executable:

```
Get-FileHash .\SnipTextProUltra.exe -Algorithm SHA256
```

**Requirements:** Windows 11, or Windows 10 version 1903 or later, 64-bit.

---

## What's new

### Screenshot to Text now reads things that aren't words

Two complaints, one cause.

`@#4!TW$RH^%&CFG?:` came back as nothing at all. And serial numbers sometimes
came back with a wrong digit.

`Windows.Media.Ocr` is a **language** recogniser. It scores what it reads
against a lexicon, discards regions containing no dictionary word, and — when
confidence is low — *rewrites* characters into whatever makes a real word.
So a symbol string is read perfectly and then thrown away, and `WO-4B21`
becomes `WO-4821` because the engine prefers something word-shaped. It was
never misreading; it was correcting. Neither behaviour has an off switch.

Tesseract does have the switches (`load_system_dawg`, `load_freq_dawg`), so
with its dictionaries disabled it reports what it actually saw. It is now
compiled in as a fallback.

### Settings → Text Recognition

| | |
|---|---|
| **Auto** (default) | Windows first, because it is fast and right nearly always. The fallback runs only when Windows came back with almost nothing — exactly the case it fails on. |
| **Windows only** | Fastest. The previous behaviour. |
| **Fallback only** | For captures that are mostly codes and symbols. |

### It is still lightweight

This was the constraint, and it shaped the design:

- **Nothing loads at startup.** Idle memory is unchanged — a message loop,
  six hotkey registrations and a tray icon.
- **The fallback engine is created per capture and destroyed with it**, so
  there is nothing resident between captures. Re-initialising costs about
  100 ms, which is the right trade for a path that only runs when the fast
  engine came back nearly empty.
- **Every resource is RAII-owned**, including the Leptonica image and the
  engine handle, so there is no branch on which one leaks.
- **Still one file.** The engine is statically linked and the model embedded
  as a resource — no DLLs, no `tessdata` folder, no network.

The cost is the 15 MB binary, and a slower CI build.

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

- **The fallback engine itself.** It compiles and is wired in, but its
  accuracy on real captures has not been measured against the macOS version.
  If a capture still comes back wrong, the log line beginning
  `ocr: Tesseract read` says what each engine managed.
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
- Table columns merge into a single line; switching *Join Wrapped Lines*
  off preserves the rows.
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
