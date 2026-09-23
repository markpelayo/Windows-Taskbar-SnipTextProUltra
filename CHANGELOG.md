# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org).

## [Unreleased]

Nothing yet.

## [1.0.0] — 2026-09-24

First release. A C++/Win32 port of
[MacOS-menubar-SnipText-ProUltra](https://github.com/markpelayo/MacOS-menubar-SnipText-ProUltra).

> **This release has never been compiled or run on a Windows machine.** It is
> a complete implementation written from the macOS source, not a verified one.
> Treat it as pre-release whatever the version number says, and read
> [What has and has not been tested](README.md#what-has-and-has-not-been-tested)
> before you rely on it.

### Added

- **Screenshot** — region or full screen, opening an annotation editor with
  arrow, rectangle, ellipse, line, freehand pen and text tools. Every mark
  stays selectable, movable, resizable and restylable; undo covers all of
  those on the same footing as drawing.
- **Screenshot to Text** — on-device OCR via `Windows.Media.Ocr`, with the
  geometry-driven line normaliser ported intact from the macOS version, to
  the clipboard.
- **Record Video** — a resizable region or a whole screen, to H.264 MP4 via
  Media Foundation, with optional microphone audio, selectable frame rate and
  quality, and mouse cursor and click capture.
- A pinnable taskbar model: the first launch stays resident, later launches
  open the menu and exit. A tray icon appears only while recording, as a
  one-click Stop.
- Global hotkeys on `Win+Alt+1` through `6`, in menu order.
- Independent output folders for screenshots, text-capture sources and
  videos, each with its own submenu.
- **Sanitize and Restore Default** — saved files to the Recycle Bin, every
  setting back to its default, itemised in the confirmation with live counts.
- Run at Startup, with an optional delay that applies only to a launch
  Windows made after a boot.
- A plain-text log at `%LOCALAPPDATA%\SnipText\Logs\SnipText.log`, with a
  verbose mode behind a registry switch, self-rotating at 512 KB.
- Assertions for the text normaliser covering the six cases that shaped the
  algorithm and the list-marker traps it has to avoid.
- `build.bat` (MSVC, finds Visual Studio itself) and `CMakeLists.txt`, plus a
  GitHub Actions workflow that builds and runs the tests on every push.

### Known issues

- **`Win+Alt+<digit>` collides with the Windows shell**, which uses it to open
  the Jump List of the pinned taskbar app in that position. The shell
  registers first, so some or all six hotkeys may fail to register. When one
  does, it silently does nothing for the session and the log names it — the
  menu item still works. Switching to `Ctrl+Alt` or `Alt+Shift` is a one-line
  change to the modifier mask in `RegisterHotkeys()`; configurable hotkeys are
  the real fix and are on the roadmap.
- Full-screen capture covers one monitor, the one under the pointer.
- Table columns merge into a single line. *Keep Line Breaks* preserves the
  rows.
- No webcam recording and no system-audio (loopback) recording. Microphone
  input works.
- Not code-signed. There is no binary download; build it yourself.

### Notes on the port

The behavioural differences from the macOS original, and the reasoning behind
each, are listed in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#deviations-from-the-macos-original).
The short version: the app model, the confirmation surface, the hotkeys, the
container format, the OCR engine and the editor's Y axis all changed because
the platform is different. Nothing else did.

[Unreleased]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.0.0
