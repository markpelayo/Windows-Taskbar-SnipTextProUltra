# SnipText Pro Ultra for Windows — v1.4.0

A pinnable Windows taskbar utility that does three things from one icon:
**screenshot with an annotation editor**, **screenshot to text** via on-device
OCR, and **screen recording** to MP4.

Two changes, both from using it: a setting whose name did not describe what it
did, and an OCR engine that gave up on text it could have read.

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

### "Keep Line Breaks" is now "Join Wrapped Lines"

Same behaviour, honest label — and it now defaults to **on**.

The old name implied that switching it off gave you one continuous line. That
was never what it did. OCR reports what it sees *on screen*, so a paragraph
that wraps over three lines arrives as three lines; the setting decides
whether to put it back together. It only ever joined lines that **wrapped** —
lines that were genuinely separate, like a column of IDs, were always left
alone.

So:

| | |
|---|---|
| **Join Wrapped Lines ✓** (default) | Wrapped prose is rejoined into paragraphs. Separate lines stay separate. |
| **Join Wrapped Lines ☐** | Every line exactly as the engine saw it. Four lines on screen, four lines of text. |

Unchecked is the predictable one: verbatim, always. Checked is the one that
tries to be clever, and is right for prose.

It is stored under a new registry key, so an existing `keepLineBreaks` value
cannot be read under the opposite meaning.

### OCR retries when it struggles

Windows' recogniser has two blind spots: light text on a dark background, and
small text. Both used to mean a capture came back partly or wholly empty.

Now the first pass is the image as captured, and if that reads little, it
tries an inverted copy and a doubled copy, keeping whichever pass read the
most. A first pass that already read a good amount short-circuits the rest, so
an ordinary capture costs exactly what it did before. The log names the pass
that won, which is the first thing to look at if a capture still comes back
short.

**This does not make it equal to the macOS version.** See the note under
*Not verified* below.

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

- **OCR on text with no words in it.** `Windows.Media.Ocr` scores what it
  reads against a lexicon and discards regions that do not look like words in
  an installed language. A string like `@#4!TW$RH^%&CFG?:` can be read
  cleanly and still be thrown away. Apple's Vision does not do this, so the
  **macOS version reads such strings and this one may not** — the retry
  passes above help when the engine could not *see* the text, not when it
  decided the text was not language. Only a different engine fixes it, at the
  cost of the no-dependencies rule. If you hit this, the log will show the
  pass that won and how little it read.
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
