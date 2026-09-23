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

**This is version 1.0.0, and it has not been run on real hardware yet.** Please read [What has and has not been tested](#what-has-and-has-not-been-tested) before you decide how much to trust it.

---

## How it behaves as a Windows app

macOS has a menu bar; Windows does not. The closest honest equivalent is a program you pin to the taskbar, which is what this is.

Pinning on Windows pins a *shortcut to the executable*, so "clicking the icon" means launching it. The first launch stays resident to hold the global hotkeys. Every later launch finds the running instance, tells it to open its menu, and exits immediately. From your side that is one icon that opens one menu — the macOS behaviour, built out of the pieces Windows actually provides.

While idle there is **no tray icon and no window**. A tray icon appears only while recording, so the red square is a one-click Stop with no menu to open first. That is the exact role the second status item plays on macOS.

### Pinning it

1. Build it (below), then run `build\SnipText.exe` once.
2. Right-click its taskbar button → **Pin to taskbar**.

That's it. The pinned icon is now the app.

---

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
| `build.bat` | Build into `build\SnipText.exe` |
| `build.bat run` | Build and launch |
| `build.bat test` | Build and run the text-normaliser tests |
| `build.bat clean` | Delete the build folder |

There is a `CMakeLists.txt` too, if you would rather:

```
cmake -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

> **No download link, on purpose.** An unsigned executable downloaded from the
> internet gets a SmartScreen warning that teaches people to click through
> SmartScreen warnings. Building it yourself is also the only way to be
> certain the binary matches the source you can read here.

### Uninstalling

```
taskkill /IM SnipText.exe /F
reg delete "HKCU\Software\markpelayo\SnipText" /f
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v SnipText /f
rmdir /s /q "%LOCALAPPDATA%\SnipText"
```

Then delete `SnipText.exe` and unpin it. Your captures are left alone — they are in `%USERPROFILE%\Pictures\SnipText_*` and `%USERPROFILE%\Videos\SnipText_Videos`. Delete those yourself if you want them gone.

---

## Shortcuts

| | |
|---|---|
| `Win+Alt+1` | Screenshot — region, opens the editor |
| `Win+Alt+2` | Screenshot — full screen, opens the editor |
| `Win+Alt+3` | Screenshot to Text — region |
| `Win+Alt+4` | Screenshot to Text — full screen |
| `Win+Alt+5` | Record — region (press again to stop) |
| `Win+Alt+6` | Record — full screen (press again to stop) |

The numbers run top to bottom in menu order, so the menu itself is the reminder.

In any region capture: drag to select, **Space** switches to click-a-whole-window, **Esc** cancels.

> **Heads up: `Win+Alt+<digit>` is already a Windows shortcut.** The shell uses
> it to open the Jump List of the pinned taskbar app in that position. The
> shell registers first, so some or all of these six may simply fail to
> register — in which case that shortcut does nothing for the session and the
> menu item is the way in. Check the log to see which ones took.
>
> If they don't take on your machine, `Ctrl+Alt+1`–`6` and `Alt+Shift+1`–`6`
> are both unclaimed by Windows 11 and are one line to switch to — the
> modifier flags are in `RegisterHotkeys()` in `src/App.cpp`.

If another program already owns one of these combinations, that one shortcut silently does nothing for the session — the menu item still works. The log says which one.

---

## The menu

```
Screenshot
  Capture Region…          Win+Alt+1
  Capture Full Screen      Win+Alt+2
  Show Saved Images (12)
──────────────────────
Screenshot to Text
  Capture Region…          Win+Alt+3
  Capture Full Screen      Win+Alt+4
  Copy: "Work Order #…"
  Show Saved Images (4)
──────────────────────
Record Video
  Record Region…           Win+Alt+5
  Record Full Screen       Win+Alt+6
  Show Saved Videos (3)
──────────────────────
Settings
  Keep Line Breaks       ✓
  Shutter Sound          ✓
  Auto-Save Images       ✓
  Screenshot Folder      ▸
  Text Folder            ▸
  Video Settings         ▸
  Video Folder           ▸
  Sanitize and Restore Default…
──────────────────────
Startup
  Run at Startup: 15 s   ▸
──────────────────────
  Quit SnipText
```

Every capture section has the same shape: **capture commands first, then the ways to get at what they produced.** Once you have read one section you can predict the others.

---

## Screenshot to Text

Text lands on the clipboard; `Ctrl+V` wherever you want it. Confirmation is the shutter sound plus a small message above the taskbar with a character count. No notification banner, which also means no notification permission prompt.

**Keep Line Breaks** is the setting you will actually reach for. Off (default), wrapped lines are joined into paragraphs — right for prose, docs, emails, error dialogs. On, you get exactly the lines the engine saw — right for code, logs, IDs, table cells.

Line joining is decided from **geometry, not character counts**: a line that stops short of the right margin didn't wrap, so its break is kept. See [the architecture notes](docs/ARCHITECTURE.md#text-normalisation) for why the obvious approach doesn't work, and `tests/TextNormalizerTests.cpp` for the cases that shaped it.

---

## The annotation editor

Opened by the Screenshot commands. Tools: **arrow, rectangle, ellipse, line, freehand pen, text**, with a colour swatch and a stroke-width slider (the slider also sets text size).

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

`Win+Alt+5` puts a resizable selection rectangle on screen — drag inside to move, drag the eight handles to resize, drag empty space to draw a new one. The readout shows the size in **recorded pixels**, accounting for the quality setting. **Enter** or the Record button starts; **Esc** cancels.

The overlay is hidden before the first frame is grabbed, so it never appears in the video.

While recording, a red square appears in the notification area. Its tooltip carries the elapsed time, and a single click stops the recording.

Quitting mid-recording stops it first and waits up to three seconds for the encoder to finish writing the MP4 index — exiting before that leaves an unplayable file.

**Video Settings** lives in the menu rather than behind a gear button, so every setting in the app is in one place: frame rate (15/24/30/60), quality, capture mouse cursor, capture mouse clicks, and audio input.

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

- **While idle there is nothing running**: no timer, no window, no tray icon, no background thread. The process exists to hold six hotkey registrations and a message loop.
- **Peak memory is one capture's bitmap** — about 8 MB for a 1440p screen, 33 MB for 4K — released as soon as OCR or the editor is finished with it.
- **One timer exists only while recording**, at one tick a second, driving both the elapsed time and the blink so they cannot drift apart.
- **Every GDI object, handle and COM pointer is owned by an RAII wrapper**, so there is no branch — including an early return — on which a resource leaks.
- Undo is capped at 50 snapshots; the log rotates at 512 KB.

Nobody has profiled it. Those are design properties, not measurements.

---

## What has and has not been tested

Stated plainly, because it matters more than any feature list.

**Not tested at all.** This port was written from the macOS source and has never been compiled or run on a Windows machine. It is a complete, careful implementation, not a verified one. Treat version 1.0.0 as a first draft that needs a real machine.

The parts most likely to need work on first contact:

- **Media Foundation encoding.** The sink-writer configuration, the RGB32 input stride and the AAC audio path are all written from documentation. Expect the recorder to be where the first bugs are.
- **The WinRT OCR plumbing.** It uses the ABI headers and WRL rather than C++/WinRT, to keep the build dependency-free. The `SoftwareBitmap` buffer dance is the fiddly part.
- **Multi-monitor and mixed-DPI setups.** The program is Per-Monitor-V2 aware and works in physical pixels throughout, which is the correct design, but "correct design" and "correct on your three-monitor desk" are different claims.

**Tested.** The text normaliser's logic, against the cases in `tests/TextNormalizerTests.cpp` — the six that shaped the algorithm plus the list-marker traps. Those expectations were checked against an independent implementation of the same algorithm before being written down.

---

## Known limitations

- **Full-screen capture is one monitor** — the one under the pointer. Capturing several at once would write files that need stitching.
- **OCR language follows Windows.** It uses your installed display languages; there is no per-capture language picker.
- **Table columns merge.** Blocks on the same visual row join with a space. "Keep Line Breaks" at least preserves the rows.
- **No webcam recording.** Compositing a webcam in needs a different encoding pipeline, not a flag.
- **No system audio recording.** Microphone input works; loopback capture of what the machine is playing is a separate WASAPI path that is not wired up.
- **No auto-paste.** Clipboard-only by design.
- **Not code-signed.** This builds from source on your own machine.

---

## Project layout

```
src/                  18 source files — see docs/ARCHITECTURE.md for the map
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
