# SnipText Pro Ultra for the Windows taskbar

[![build](https://img.shields.io/badge/build-CMake%20%2F%20MSVC-informational)](.github/workflows/build.yml)
[![platform](https://img.shields.io/badge/platform-Windows%2011%20x64-0078D4)](#requirements)
[![licence](https://img.shields.io/badge/licence-MIT-blue)](LICENSE)

A pinnable taskbar utility that does three things from one icon:

- **Screenshot** → opens an annotation editor (arrows, shapes, freehand, text) where every mark stays editable
- **Screenshot to Text** → on-device OCR straight to your clipboard
- **Record Video** → a resizable region or a whole screen, to `.mp4`

It is a C++/Win32 port of [MacOS-menubar-SnipText-ProUltra](https://github.com/markpelayo/MacOS-menubar-SnipText-ProUltra), which does the same three things from the macOS menu bar.

Everything runs locally. OCR is Windows' own `Windows.Media.Ocr`, capture is GDI, encoding is Media Foundation. No network, no accounts, no subscription, no telemetry.

One executable, no installer, no third-party dependencies — nothing but the Windows SDK. The binary is statically linked, so there is no runtime to install.

This is version 1.0.0. It builds clean under `/W4 /WX` and has been run on Windows 11 on one machine. Please read [What has and has not been tested](#what-has-and-has-not-been-tested) before you decide how much to trust it.

---

## How it behaves as a Windows app

macOS has a menu bar; Windows does not. The closest honest equivalent is a program you pin to the taskbar, which is what this is.

Pinning on Windows pins a *shortcut to the executable*, so "clicking the icon" means launching it. The first launch stays resident to hold the global hotkeys. Every later launch finds the running instance, tells it to open its menu, and exits immediately. From your side that is one icon that opens one menu — the macOS behaviour, built out of the pieces Windows actually provides.

A **tray icon** sits in the notification area whenever the program is running, so you can always tell that it is and always get at its menu. Either mouse button on it opens the same menu the pinned taskbar icon does. There is no window at all.

### Pinning it

1. Build it (below), then run `build\SnipTextProUltra.exe` once.
2. Right-click its taskbar button → **Pin to taskbar**.

That's it. The pinned icon is now the app.

---

## Download

`SnipTextProUltra.exe` is attached to [the latest release](https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/latest). One file, no installer, nothing to put beside it — the binary is statically linked.

> **Windows will warn you about it.** The executable is not code-signed, so SmartScreen shows *"Windows protected your PC"*. That warning is correct, and worth reading rather than reflexively clicking past: it means Windows cannot tell who published this. The honest answer is that it was published by one person with no certificate.
>
> If you want to run it anyway: **More info → Run anyway**.
>
> If you would rather not click through a SmartScreen warning — a reasonable position, and the habit is worth protecting — [build it yourself](#build) instead. It is two commands, and it is the only way to be certain the binary matches the code you can read here.

A `SHA-256` checksum is published beside each release binary. It is not a signature and does not pretend to be one; it only lets you confirm the file you downloaded is the file CI produced:

```
Get-FileHash .\SnipTextProUltra.exe -Algorithm SHA256
```

## Requirements

- Windows 11 (or Windows 10 version 1903 or later)
- Visual Studio 2022 with the **Desktop development with C++** workload, or the Build Tools
- An OCR language installed. Windows ships one with your display language; if *Screenshot to Text* says no language is installed, add one under **Settings → Time & language → Language & region**.

## Build

```
git clone https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra.git
cd Windows-Taskbar-SnipTextProUltra
build.bat run
```

`build.bat` finds Visual Studio on its own if you are not already in a developer command prompt.

| | |
|---|---|
| `build.bat` | Build into `build\SnipTextProUltra.exe` |
| `build.bat run` | Build and launch |
| `build.bat test` | Build and run the text-normaliser tests |
| `build.bat clean` | Delete the build folder |

There is a `CMakeLists.txt` too, if you would rather:

```
cmake -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

Building from source is the recommended route. It is the only way to be certain the binary matches the code you can read here, and it sidesteps the SmartScreen warning that an unsigned download will always produce.

### Building with the second OCR engine

The default build uses only `Windows.Media.Ocr` and needs nothing but the Windows SDK. Released binaries additionally embed Tesseract, which is what lets the app read serial numbers, hashes and symbol strings — see [Screenshot to Text](#screenshot-to-text).

```
vcpkg install --triplet x64-windows-static
cmake -B build -A x64 -DSNIPTEXT_WITH_TESSERACT=ON ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

The trained model (~4 MB) is fetched at configure time by `scripts/fetch-tessdata.ps1` and compiled in as a resource, so the finished executable is still one file. Its SHA-256 must be pinned in that script before CI will build a release. To work the hash out from macOS or Linux, where `pwsh` is usually absent:

```
./scripts/tessdata-hash.sh
```

That downloads the model to a temporary file, prints the line to paste, and deletes it again.

### Uninstalling

```
taskkill /IM SnipTextProUltra.exe /F
reg delete "HKCU\Software\markpelayo\SnipText" /f
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v SnipText /f
rmdir /s /q "%LOCALAPPDATA%\SnipText"
```

Then delete `SnipTextProUltra.exe` and unpin it. Your captures are left alone — they are in `%USERPROFILE%\Pictures\SnipText_*` and `%USERPROFILE%\Videos\SnipText_Videos`. Delete those yourself if you want them gone.

---

## Shortcuts

| | |
|---|---|
| `Ctrl+Shift+1` | Screenshot — region, opens the editor |
| `Ctrl+Shift+2` | Screenshot — full screen, opens the editor |
| `Ctrl+Shift+3` | Screenshot to Text — region |
| `Ctrl+Shift+4` | Screenshot to Text — full screen |
| `Ctrl+Shift+5` | Screen record — region (press again to stop) |
| `Ctrl+Shift+6` | Screen record — full screen (press again to stop) |

Those are the defaults. The numbers run top to bottom in menu order, so the menu itself is the reminder.

In any region capture: drag to select, **Space** switches to click-a-whole-window, **Esc** cancels.

### Changing them

**Settings → Change Keyboard Shortcut** lists all six with their current bindings. Pick one and a small window appears; press the combination you want and Enter to save.

Anything the keyboard can produce works, including a bare function key — press `F9` and that is the binding, no modifier required. **Delete** unbinds a shortcut entirely, leaving the action reachable only from the menu. **Esc** cancels without changing anything, and **Reset to Defaults** puts all six back.

While that window is open the app's own shortcuts stand down. They have to: a global hotkey fires before the foreground window sees the key, so otherwise pressing the shortcut you were trying to change would trigger its action instead of being captured.

If another program already owns a combination, `RegisterHotKey` refuses it. That shortcut then silently does nothing for the session and the log names it — the menu item still works, and you can pick a different combination.

---

## The menu

```
SnipTextProUltra · v1.5.0 · markpelayo
──────────────────────
  Screenshot a Region…            Ctrl+Shift+1
  Screenshot Full Screen          Ctrl+Shift+2
──────────────────────
  ScreenshotToText a Region…      Ctrl+Shift+3
  ScreenshotToText Full Screen    Ctrl+Shift+4
  Copy: "Work Order #…"
──────────────────────
  Screen Record a Region…         Ctrl+Shift+5
  Screen Record Full Screen       Ctrl+Shift+6
──────────────────────
  Change Keyboard Shortcut ▸
  Text Layout            ▸
  Shutter Sound          ✓ ▸
  After a Screenshot     ▸
  Auto-Save Images
  Save Locations         ▸
  Show Saved Files       ▸
  Screen Recording Settings ▸
  Sanitize and Restore Default…
──────────────────────
✓ Run at Startup: 15 s   ▸
──────────────────────
  Quit SnipTextProUltra
```

**Save Locations** is where each capture type writes; **Show Saved Files** opens what it wrote. Both group the same three folders:

```
  Screenshots (12)
  ScreenshotToText Images (4)
  Videos — none yet
```

*Show Saved Files* is greyed out when all three folders are empty, so it tells you there is nothing there rather than opening onto three dead entries.

**Shutter Sound** carries its own submenu: *Off*, five built-in tones, *Custom Sound…* for a `.wav` of your own, and *Preview*.

The five are **Classic** (the default), **SLR Camera**, **Aperture**, **Soft Click** and **Snap**. Choosing one plays it, so you can run down the list and compare. All five are synthesised rather than shipped as audio files, which is what keeps the executable a single self-contained binary with no `.wav` beside it; each is generated the first time you pick it, so a tone you never choose costs nothing.

On *Aperture*: people ask for "the macOS screenshot sound". Apple's is their audio asset and isn't something to copy into this binary. *Aperture* is an original synthesis with a similar character — bright and tight, a quick two-stage click rather than a heavy mechanical thunk. A family resemblance, not a reproduction.

Each section is just its commands, and everything those commands produced lives together under **Show Saved Files**. The command names carry the section, so there are no headers repeating what the row beneath already says.

The first row names the program, its version and its author, so a screenshot of the menu is enough to tell someone which build you are on. It is also a command: clicking it opens the repository.


---

## Screenshot to Text

Text lands on the clipboard; `Ctrl+V` wherever you want it. Confirmation is the shutter sound plus a small message above the taskbar with a character count. No notification banner, which also means no notification permission prompt.

### Text Layout

OCR reports what it sees **on screen**, not what the text means. A paragraph that wraps over three lines arrives as three separate lines, and the space between two blocks arrives as nothing at all. This setting decides whether to put the structure back.

| | |
|---|---|
| **Rebuild Paragraphs** (default) | Lines that wrapped are rejoined, and a blank line goes in wherever the original had a bigger gap. Right for docs, emails, articles, error dialogs. |
| **Keep Every Line Separate** | Every line exactly as the engine saw it, nothing joined and nothing inserted. Right for code, logs, IDs, chat lists, table cells. |

Two rows rather than a checkbox, and that is the third name this setting has had. *Keep Line Breaks* described the state you were switching **away** from. *Join Wrapped Lines* named only half of what the other state does — and on a capture with nothing wrapped in it, a chat list or a table where every line is already truncated with an ellipsis, that half does nothing, so the only visible effect was blank lines the name never mentioned. A checkbox can only ever name one of its two states; naming both costs one row and ends the guessing.

**Rebuild** does not mean "join everything". It only joins lines that actually **wrapped**, and that decision comes from **geometry, not punctuation**: a line is a continuation only if all four hold — the previous line ran to the right margin, the vertical gap is normal leading, the current line is not indented, and it does not start a list item. A column of work-order numbers stays a column, because each of those lines stopped well short of the right margin.

The blank lines come from the same measurements: a gap wider than 1.6× the median line height is treated as a block break rather than a line break.

See [the architecture notes](docs/ARCHITECTURE.md#text-normalisation) for why the obvious approach doesn't work, and `tests/TextNormalizerTests.cpp` for the cases that shaped it.

**Keep Every Line Separate** is the predictable one — verbatim, always. Reach for it when a capture comes back joined, or spaced out, in a way you did not want.

---

## After a Screenshot

By default a Screenshot command opens the annotation editor. **Settings → After a Screenshot** lets you skip it:

| | |
|---|---|
| **Open the Editor** (default) | The capture opens for marking up — arrows, boxes, text. |
| **Copy to Clipboard and Close** | The capture goes straight to the clipboard and nothing opens. `Ctrl+V` anywhere. |

*Copy to Clipboard and Close* is the quick path: shutter sound, a small confirmation above the taskbar with the pixel size, and you're already able to paste. Nothing to close, nothing to dismiss.

Two things worth knowing:

**Auto-Save applies either way.** With both on, the shot is written to disk *and* put on the clipboard, and still nothing opens.

**It only affects the two Screenshot commands.** *Screenshot to Text* never opened the editor, so it is unchanged, and so is recording.

If the clipboard write fails — another program can hold the clipboard open — you get a message saying so rather than silence, because silence would be indistinguishable from success and the capture would be gone.

---

## The annotation editor

Opened by the Screenshot commands. Tools: **arrow, rectangle, ellipse, line, freehand pen, text, lift**, with a colour swatch and a stroke-width slider (the slider also sets text size).

### Lift

Every other tool draws on top of the picture. **Lift** moves the picture itself: drag a rectangle over any part of the capture and that region becomes a piece you can drag somewhere else in the same image.

It is for the times when pointing at something is weaker than showing it. Instead of an arrow saying *this button belongs over there*, put the button over there.

| | |
|---|---|
| **drag** | Copy. The original stays where it was; pull the piece aside and it is still there. |
| **Shift**+drag | Cut. The source is blanked with a colour sampled from the pixels just around it, so pulling the piece aside reveals a patch rather than the original. |

Either way the piece lands exactly on top of where it came from, so nothing appears to happen until you drag it — which is the point. What you do next says whether it was a copy or a move.

You do not have to remember the Shift part: while Lift is the selected tool, the canvas shows *Drag to copy a piece · Shift-drag to cut it out* along the bottom. It appears only for this tool, because Lift is the only one with a modifier, and it disappears while you are dragging — by then the choice is already made.

A lifted piece is an ordinary mark: select it, drag it, resize it from its handles, undo it. **The capture underneath is never modified**, so nothing is destroyed and every lift is reversible, including the Shift one — the blanked patch is part of the mark, not a change to the pixels.

On Shift: the fill is the most common colour in a two-pixel ring around the region, which is exact on a flat background and visibly a patch on a gradient or a photo. That is why plain drag, which never leaves a hole, is the default.

| | |
|---|---|
| `Ctrl+C` | Copy the annotated image to the clipboard |
| `Ctrl+S` | Save as PNG |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |

Clicking the colour swatch drops a grid of nine presets — one click, no dialog. The colour-wheel cell opens the full system colour picker.

Click with the text tool to type a label in place; Enter commits, Esc discards.

### Marks stay editable

Nothing is baked in until you copy or save.

| | |
|---|---|
| Click a mark | Select it (dashed outline + handles) |
| Drag it | Move it |
| Drag a handle | Resize — 8 on rectangles and ellipses, 2 endpoints on lines and arrows |
| `Delete` | Remove the selected mark |
| `Esc` | Deselect |

Shapes are hit on their **outline**, not their interior, so clicking inside an empty rectangle starts a new drawing rather than grabbing the rectangle.

Undo is snapshot-based: moves, resizes, restyles and deletions are all undoable on the same footing as drawing.

**Your settings persist** between editor windows — set the slider to maximum once and the next screenshot opens with it.

Annotations are stored in **image pixel coordinates**, so the PNG you save is full resolution and matches what you saw, whatever size the window was.

---

## Recording video

`Ctrl+Shift+5` puts a resizable selection rectangle on screen — drag inside to move, drag the eight handles to resize, drag empty space to draw a new one. The readout shows the size in **recorded pixels**, accounting for the quality setting. **Enter** or the Record button starts; **Esc** cancels.

The overlay is hidden before the first frame is grabbed, so it never appears in the video.

### Knowing that it is recording

Two things appear the moment a recording starts:

- **A green dashed frame** around the recorded area, so there is never any doubt about what is being captured. For a region it is drawn strictly *outside* the captured rectangle, so it cannot appear in the video. It is painted once and then costs nothing.
- **A Stop pill** — `● 00:24` — placed just outside the frame. It carries the elapsed time, its dot pulses once a second, and one click stops the recording.

**Full-screen recordings get the frame too**, which they previously did not. A region covering the whole monitor has no outside to put a border in, so for that case the frame is drawn just *inside* the screen edges and hidden from the capture with `SetWindowDisplayAffinity` — the Windows mechanism intended for exactly this, whose own documentation gives "windows that show video recording controls" as the example. It needs **Windows 10 version 2004 or later**; on anything older the frame stays outside, which means a full-screen recording has no frame, exactly as before. The program checks that the flag really took effect rather than assuming it, because a border wrongly believed to be hidden would be recorded into every video.

The same mechanism keeps the Stop pill out of the video when it has to sit inside the recorded area — which used to be an accepted limitation noted in the log.

The menu also carries a red dot beside **Stop Recording (00:04)** while a recording is running, so opening it tells you at a glance.

A red square also appears in the notification area as a second way to stop. It is not the indicator, though — Windows 11 collapses the notification area behind a chevron by default, so anything that lives only there is invisible to most people.

Stopping a recording removes all three.

Quitting mid-recording stops it first and waits up to three seconds for the encoder to finish writing the MP4 index — exiting before that leaves an unplayable file.

**Screen Recording Settings** lives in the menu rather than behind a gear button, so every setting in the app is in one place: frame rate (15/24/30/60), quality, file size, capture mouse cursor, capture mouse clicks, and audio input.

**Quality** and **File Size** are different axes, and it is worth knowing which one you want. Quality scales the picture down — 75% or 50% of the captured size — so the file shrinks and the video gets blurrier when you zoom in. File Size leaves the resolution alone and changes how many bits are spent on it:

| | |
|---|---|
| **Smaller** (default) | About half the size of the old default |
| **Balanced** | What every recording before v1.6 used |
| **Detailed** | For fine text and gradients |

Smaller is the default rather than a compromise. A desktop barely changes from frame to frame, which is the case H.264 handles best, so the old fixed bitrate was spending most of its budget encoding a static background very precisely.

**Use H.265 When Available** halves the size again. It is off by default and should stay off unless you know where the file is going: H.265 needs a reasonably modern player, and a recording that won't open on the machine you sent it to isn't a smaller file, it's a broken one. If the machine has no HEVC encoder the recorder falls back to H.264 on its own, so switching it on can never cost you a recording.

### Why there is no MKV or AVI option

Two separate reasons, and the second is the one that matters.

Media Foundation picks a media sink from the file extension, and the sinks Windows ships are MPEG-4 (`.mp4`, `.m4v`, `.3gp`) and ASF (`.wmv`, `.asf`). There is an MKV *source* — Windows 10 can play Matroska — but no MKV sink, and no AVI sink in either direction. Writing either one means embedding a third-party muxer, and a static FFmpeg is tens of megabytes against this program's entire budget.

But a container doesn't compress anything. It's an index and a wrapper around streams that are already encoded. Remuxing the same H.264 stream from MP4 to MKV changes the file by a few kilobytes of header across an entire recording — well under a tenth of a percent. MKV files are often smaller than MP4 files you've seen, but that's because they were *encoded* differently, not because of the container. What sets the size is the codec and the bitrate, which is what File Size and H.265 control.

---

## Where files go

**Auto-Save Images** is off by default. Turn it on and every capture is saved as a PNG, with each capture type in its own folder:

```
%USERPROFILE%\Pictures\SnipText_Screenshot_Images\          ← Screenshot captures
%USERPROFILE%\Pictures\SnipText_ScreenshotToText_Images\    ← Screenshot to Text sources
%USERPROFILE%\Videos\SnipText_Videos\                       ← recordings
```

Each has its own **Folder ▸** submenu, so any of the three can be pointed elsewhere independently. Filenames include milliseconds, because two captures in the same second would otherwise overwrite each other.

**Nothing is ever auto-deleted.** These are your files in your Pictures and Videos folders.

**Sanitize and Restore Default…** gives you a clean slate: every saved image and video goes to the **Recycle Bin** (not deleted — you can put them back), and all settings return to their defaults. The confirmation itemises exactly what will happen with live file counts, and Cancel is the default button.

---

## Resource usage

It is deliberately lightweight, and the design reasons are in [the architecture notes](docs/ARCHITECTURE.md#resource-behaviour). In short:

- **While idle there is nothing running**: no timer, no window, no background thread. The process exists to hold six hotkey registrations, one tray icon and a message loop.
- **Peak memory is one capture's bitmap** — about 8 MB for a 1440p screen, 33 MB for 4K — released as soon as OCR or the editor is finished with it.
- **One timer exists only while recording**, at one tick a second, driving the elapsed time, the pulse and the Stop pill so they cannot drift apart. The green frame is painted once and never repaints; the pill repaints about 150×34 pixels a second, which is the entire ongoing cost of the recording indicator.
- **Every GDI object, handle and COM pointer is owned by an RAII wrapper**, so there is no branch — including an early return — on which a resource leaks.
- Undo is capped at 50 snapshots; the log rotates at 512 KB.

Nobody has profiled it. Those are design properties, not measurements.

---

## What has and has not been tested

Stated plainly, because it matters more than any feature list.

### Verified

- **It builds**, clean, under `/W4 /WX` with MSVC — so there is not a single compiler warning in the tree — and the CI job does it on every push.
- **It runs.** On Windows 11, on one machine, at one resolution. The menu opens, the region overlay works, the recorder produces files.
- **The text normaliser's logic**, against the twenty assertions in `tests/TextNormalizerTests.cpp` — the six cases that shaped the algorithm plus the list-marker traps. Those expectations were checked against an independent implementation of the same algorithm before being written down.

### Not verified

Everything else. In rough order of how likely it is to bite:

- **Video output quality.** Recordings are produced, but nobody has checked them frame by frame across frame rates, quality settings, or with audio on. The Media Foundation sink-writer configuration was written from documentation.
- **Microphone audio.** The WASAPI capture and AAC path have not been exercised at all. They are written to fail soft — a bad microphone gives you a silent video, never a lost one — but "fails soft" is a design claim, not a measurement.
- **Multi-monitor and mixed-DPI setups.** The program is Per-Monitor-V2 aware and works in physical pixels throughout, which is the correct design, but "correct design" and "correct on your three-monitor desk" are different claims. The startup log prints your display layout, which is the first thing to check if a capture lands in the wrong place.
- **The annotation editor under sustained use.** Individual tools work. Long sessions, deep undo stacks, and the interaction between text entry and the other tools have not been hammered.
- **Long-running behaviour.** The idle cost is designed to be zero and every resource is RAII-owned, but nobody has left it running for a week and watched the handle count.

Nobody has profiled it, either. The figures under [Resource usage](#resource-usage) are design properties, not measurements.

---

## Known limitations

- **Full-screen capture is one monitor** — the one under the pointer. Capturing several at once would write files that need stitching.
- **OCR language follows Windows.** It uses your installed display languages; there is no per-capture language picker.
- **Table columns merge.** Blocks on the same visual row join with a space. Switching *Text Layout* to *Keep Every Line Separate* at least preserves the rows.
- **No webcam recording.** Compositing a webcam in needs a different encoding pipeline, not a flag.
- **No system audio recording.** Microphone input works; loopback capture of what the machine is playing is a separate WASAPI path that is not wired up.
- **No auto-paste.** Clipboard-only by design.
- **Not code-signed.** This builds from source on your own machine.

---

## Project layout

```
src/                  20 source files — see docs/ARCHITECTURE.md for the map
tests/                the text-normaliser assertions
assets/SnipText.ico   the application icon
build.bat             MSVC build, no CMake needed
CMakeLists.txt        the same build, for people who prefer CMake
VERSION               single source of truth for the version number
docs/ARCHITECTURE.md  how it works and why
docs/RELEASING.md     how a version is cut
```

## Documentation

- **[Architecture](docs/ARCHITECTURE.md)** — how it works, the interesting problems, and the bugs worth knowing about
- **[Changelog](CHANGELOG.md)** — release history
- **[Release notes — v1.0.0](docs/RELEASE-NOTES-v1.0.0.md)** — what shipped, and what to watch for
- **[Disclaimer](DISCLAIMER.md)** — no-warranty and liability terms, including what you are responsible for when you record a screen

## Troubleshooting

### The log

```
notepad "%LOCALAPPDATA%\SnipText\Logs\SnipText.log"
```

**While this is still a 1.x shakedown build, verbose logging is on by default**, so the log carries a full per-stage trace of every capture rather than one line each. It self-rotates at 512 KB, so it cannot grow without bound.

Every launch starts with an environment block, which is what makes a log someone sends back self-contained:

```
[2026-09-24 14:06:58.031] ———— SnipText launched, log at C:\Users\…\SnipText.log
[2026-09-24 14:06:58.034] env: SnipText 1.0.0 (x64, built Sep 24 2026 11:02:17)
[2026-09-24 14:06:58.036] env: Windows 11 10.0 build 26100
[2026-09-24 14:06:58.041] env: display 0 [primary] 3840x2160 at (0,0), 192 dpi (200%)
[2026-09-24 14:06:58.042] env: display 1           1920x1080 at (3840,0), 96 dpi (100%)
[2026-09-24 14:06:58.043] env: virtual desktop 5760x2160 at (0,0), DPI awareness PerMonitorV2 (correct)
[2026-09-24 14:06:58.088] env: OCR engine available
[2026-09-24 14:06:58.089] env: screenshots  -> C:\Users\…\Pictures\SnipText_Screenshot_Images
```

**If you are reporting a bug, paste that block.** It answers the three questions that otherwise take a round trip: which Windows build, what the display layout is, and whether an OCR language is installed at all.

To go quiet on one machine without rebuilding:

```
reg add "HKCU\Software\markpelayo\SnipText" /v debugMode /t REG_DWORD /d 0 /f
```

then quit and relaunch. Set it to `1` to force it back on.

> **Reverting this later.** Verbose-by-default and the environment block are
> temporary. Flip `kVerboseByDefault` to `false` in `src/Log.h` and delete the
> `WriteStartupDiagnostics()` call in `App::Run()`. Nothing else depends on
> either.

**A hotkey does nothing.** Something else owns that combination. The log names it at startup; the menu item still works.

**"Screenshot to Text" says no language is installed.** Add one under Settings → Time & language → Language & region. Windows OCR only recognises languages you have installed.

**The recording is a file no player will open.** The MP4 index is written when the recording stops. If the process was killed mid-recording, that never happened. Quitting through the menu waits for it.

## Versioning

This project follows [Semantic Versioning](https://semver.org). The version lives in the `VERSION` file. Releases are tagged `vMAJOR.MINOR.PATCH`; see [CHANGELOG.md](CHANGELOG.md) for the history and [docs/RELEASING.md](docs/RELEASING.md) for how a release is cut.

## Disclaimer

**This software is provided free of charge, "AS IS" and "AS AVAILABLE", without warranty or support of any kind, and is used entirely at your own risk.** Version 1.0.0 has never been compiled or run on a Windows machine — treat it as pre-release whatever the version number says.

Three things deserve emphasis:

- **OCR is approximate.** Characters may be misread, omitted or invented, and an error can be indistinguishable from correct output. Digits, identifiers, amounts and dates are especially prone to it. Verify anything that matters against the original — this is not a certified transcription tool.
- **Recording may require consent.** A capture includes whatever is on screen, and a recording includes whatever the selected microphone hears. Recording a call or meeting may legally require the consent of some or all participants depending on your jurisdiction. The app gives no notice to anyone that recording is taking place. Complying with the law, and with any workplace or platform policy, is your responsibility.
- **Captured content is yours to look after.** Screenshots and recordings routinely contain credentials, personal data, customer records and unreleased work. Nothing here inspects, redacts or encrypts any of it: files are written unencrypted to your Pictures and Videos folders, and recognised text goes on the clipboard, which any process in your session can read.

To the maximum extent permitted by law the author accepts no liability of any kind arising from its use, including damage to any machine, system or data, or any inaccuracy in or unauthorised disclosure of any information.

Full terms: [DISCLAIMER.md](DISCLAIMER.md) and the [MIT Licence](LICENSE).

## Contributing

Issues and pull requests are welcome. Two things to keep in mind: the app has no dependencies and builds with the Windows SDK alone — please keep it that way — and the idle cost should stay near zero, since this is a program people leave running for weeks. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#resource-behaviour) explains what that means in practice.

## License

MIT © Mark Pelayo — see [LICENSE](LICENSE).
