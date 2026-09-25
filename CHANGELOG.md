# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org).

## [Unreleased]

### Fixed

- **The hint bar smeared a trail across the screen while dragging a
  selection.** The overlay repaints the union of the old and new selection
  rectangles, grown by a fixed 90 px to take in the border, the handles and
  the size readout. The hint bar — *Drag to select · Space to pick a window ·
  Esc to cancel* — is 560 px wide and centred on the selection, so for any
  selection narrower than 380 px it stuck out past that region at both ends,
  and the parts sticking out were never repainted. The Record button had the
  same problem, and both are worse than a fixed padding can fix: each one
  flips to the other side of the selection when it runs out of room, moving an
  arbitrary distance in a single step. Both rectangles are now asked for
  before and after the selection changes and included explicitly.

- **Choosing a shutter sound silently reset Text Recognition to Auto.** The
  handler for *Built-in Shutter* had picked up a stray
  `settings::Remove(kOcrEngine)` from a bad paste. Nothing reported it because
  both settings are invisible until you open the menu, and the menu redraws
  from the registry, so the check mark moved and looked deliberate. The menu
  row that carried it is gone, and the line with it.

### Added

- **Five built-in shutter sounds** instead of one: Classic (the existing
  sound, unchanged and still the default), SLR Camera, Aperture, Soft Click
  and Snap. Choosing one plays it, so the list auditions itself. All five come
  from one parameterised synthesiser rather than five copies of the loop, and
  each is generated on first use — a tone you never pick is never built, so
  the cost stays one ~18 KB buffer for the run.

  On "make it sound like macOS": Apple's screenshot sound is their audio asset
  and is not something to copy into this binary. *Aperture* is an original
  synthesis with a similar character — bright and tight, a quick two-stage
  click rather than a heavy mechanical thunk. A family resemblance, not a
  reproduction.

- **File Size** under Video Settings: Smaller (new default), Balanced (the
  previous behaviour) and Detailed, plus an opt-in **Use H.265 When
  Available**. Screen content is mostly unchanged from frame to frame and
  compresses far better than camera footage, so the old fixed bitrate was
  spending bits encoding a static desktop very precisely.

- **Dev builds identify themselves.** A build from a working tree now reads
  `v1.5.0 (808fa04)` in the menu title and the log, with a trailing `+` when
  the tree had uncommitted changes; a tagged release still reads `v1.5.0`.
  During a round of UI changes the version number is identical on every build
  and so cannot answer "is this the thing I just changed?" — the question
  behind the v1.2.0 and v1.3.0 stale-tag mess.

### Changed

- Menu labels, so that every row says which of the three things it belongs to
  rather than relying on its position to imply it:
  - *Shortcuts* → **Change Keyboard Shortcut**
  - *Record Region…* → **Screen Record a Region…**
  - *Record Full Screen* → **Screen Record Full Screen**
  - *Video Settings* → **Screen Recording Settings**

  The shortcut-picker list uses the same names, so it stays a list of the
  commands rather than a second set of names for them.

- **Show Saved Files** and **Screen Recording Settings** swapped places, so
  the settings row sits immediately above *Sanitize and Restore Default*.

- **Join Wrapped Lines is now a Text Layout submenu** with both states named:
  *Rebuild Paragraphs* (default) and *Keep Every Line Separate*.

  This is the third name this setting has had, and the previous two failed the
  same way. *Keep Line Breaks* described the state you were switching away
  from. *Join Wrapped Lines* described only half of what the other state does:
  the geometry pass both rejoins wrapped lines **and** inserts a blank line
  where the original had a bigger gap. On a capture with nothing wrapped in it
  — a chat list, a table, anything already truncated with an ellipsis — the
  joining half does nothing at all, so the only visible effect was blank lines
  that the name never mentioned.

  The common factor is the checkbox. It can only name one of its two states,
  so the other is always inferred, and a wrong inference stays invisible until
  someone compares two captures side by side. Naming both states costs one
  row.

  The registry key is unchanged (`joinWrappedLines`), so nobody's setting
  resets on upgrade.

- **CI runs the fast build on every push and the slow one only on tags.**
  Every commit used to wait about twenty minutes for a full static Tesseract
  build via vcpkg before anything was checked. The dependency-free build and
  the tests now run first and always; the Tesseract build runs on `v*` tags
  and on a manual dispatch. One step decides which binary the run ships, so a
  release cannot quietly attach the dependency-free one.

### Not done — and why

- **MKV and AVI recording.** Neither is possible here, and neither would help.
  Media Foundation picks a media sink from the file extension, and the sinks
  Windows ships are MPEG-4 (`.mp4`, `.m4v`, `.3gp`) and ASF (`.wmv`, `.asf`).
  There is an MKV *source* — Windows 10 can play Matroska — but no MKV sink,
  and no AVI sink in either direction. Writing either means embedding a
  third-party muxer, and a static FFmpeg is tens of megabytes against this
  program's entire budget.

  More to the point, a container does not compress anything: it is an index
  and a wrapper around streams that are already encoded. Remuxing the same
  H.264 stream from MP4 to MKV changes the file by a few kilobytes over an
  entire recording. MKV files are often smaller because they were *encoded*
  differently, not because of the container. The codec and the bitrate are
  what set the size, so those are what the new File Size menu exposes.

## [1.5.0] — 2026-09-26

### Added

- **A second OCR engine, for text that isn't words.** Windows.Media.Ocr is a
  *language* recogniser: it scores what it reads against a lexicon, discards
  regions containing no dictionary word, and rewrites low-confidence
  characters into whatever makes a word. Neither behaviour can be switched
  off. That is why `@#4!TW$RH^%&CFG?:` came back as nothing, and why a serial
  number sometimes came back with the wrong digit — the engine was not
  misreading it, it was correcting it.

  Tesseract has explicit switches for both, so with its dictionaries disabled
  it reports what it actually saw. It is statically linked and its trained
  model is embedded as a resource, so the executable is still **one
  self-contained file** with nothing to install and no network access.

- **Text Recognition** in Settings, with three choices:
  - **Auto** (default) — Windows first, because it is fast and right nearly
    always; the fallback runs only when Windows came back with almost
    nothing, which is exactly the case it fails on.
  - **Windows only** — fastest, previous behaviour.
  - **Fallback only** — for captures that are mostly codes and symbols.

  Nothing is loaded at startup and the fallback engine is created per capture
  and destroyed with it, so **idle memory is unchanged**: the program still
  holds nothing but a message loop, its hotkeys and a tray icon.

### Notes

- The executable grows from about 1 MB to roughly 15 MB — the static engine
  plus the 4 MB model. `build.bat` still produces the small, dependency-free
  build with Windows OCR alone; the fallback is a CMake option
  (`-DSNIPTEXT_WITH_TESSERACT=ON`) and is what the released binary uses.

## [1.4.0] — 2026-09-26

### Changed

- **"Keep Line Breaks" is now "Join Wrapped Lines", and defaults to on.**
  Same behaviour, honest label. The old name implied that switching it off
  gave you one continuous line, which was never true: the setting only ever
  rejoined lines that *wrapped*, and left genuinely separate lines alone. It
  is stored under a new registry key, so an existing `keepLineBreaks` value
  cannot be read under the opposite meaning.

### Improved

- **OCR now retries a capture it struggled to read.** The first pass is the
  image as captured; if that finds little, it tries an inverted copy (for
  light text on a dark background, which the engine reads noticeably worse)
  and a 2x copy (for small text), keeping whichever pass read the most. A
  pass that already read a good amount short-circuits the rest, so an
  ordinary capture costs exactly what it did before. The log names the pass
  that won.

## [1.3.1] — 2026-09-26

The first release to actually contain any of this.

`v1.2.0` and `v1.3.0` were both tagged before their commits existed, so both
pointed at the same older commit: their binaries reported **1.1.0** and their
release bodies fell back to the whole changelog. Neither ever shipped what it
claimed to. Everything intended for those two versions is here.

CI now refuses to build a tag whose `VERSION` file does not match it, or
whose release notes are missing, so a tag can no longer quietly describe
something other than what it builds.

### Added

- **A permanent tray icon.** The program is now visibly running and always
  reachable: either mouse button on the icon opens the menu. Previously
  nothing appeared in the tray while idle, which meant Task Manager was the
  only way to confirm it was alive and the pinned taskbar icon was the only
  way to reach Quit. While recording, the same icon alternates with a red
  square once a second rather than a second icon appearing, so the tray slot
  never moves.
- **A real shutter sound**, synthesised at startup: two transients about
  70 ms apart, each a short filtered noise burst with a low tone behind it.
  Windows ships no camera-shutter sound, and the system alias in use until
  now was the "you can't click that" ding — exactly the wrong message for a
  capture that worked. Generating it keeps the executable a single
  self-contained file.
- **A custom shutter sound.** *Shutter Sound* is now a submenu: *Off*,
  *Built-in Shutter*, *Custom Sound…* for a `.wav` of your own, and
  *Preview*. A custom file that has gone missing falls back to the built-in
  sound rather than leaving the capture silent.

### Changed

- **Clearer command names.** *Screenshot a Region…* and *ScreenshotToText a
  Region…* read as instructions rather than labels.
- **The menu lost its capture-section headers.** The command names carry them
  now, so a header would just repeat the row beneath it. The *Startup* header
  went the same way, since the row under it already began with "Run at
  Startup". The *Settings* header stays: it groups rows that do not otherwise
  announce themselves.
- **The three *Show Saved* rows became one *Show Saved Files* submenu**,
  sitting just above *Sanitize and Restore Default*. They are the same kind
  of thing, and grouping them leaves each capture section as nothing but its
  commands. The parent row is greyed out when all three folders are empty,
  which is how the individual rows used to behave.
- **The title row is shorter**, and carries the version with a `v`:
  `SnipTextProUltra · v1.3.0 · by markpelayo`. It was the widest row in the
  menu and therefore set the width of every row beneath it; the repository's
  `Windows-Taskbar-` prefix says which platform it targets, which the program
  running on that platform does not need to be told.
- **The *Settings* header is gone.** The rows under it are visibly settings,
  and the separator above already marks the break.
- **The three folder rows became one *Save Locations* submenu**, above *Show
  Saved Files*. Three top-level rows, each able to grow a `": FolderName"`
  suffix, were the second-widest thing in the menu. *Text Folder* is now
  *ScreenshotToText Images*, matching the command that writes there.
- **Renamed to SnipTextProUltra throughout** — the tray tooltip, the Quit
  row, the window title and the executable's version resource, so Task
  Manager and the tray agree with each other.

## [1.1.0] — 2026-09-26

The first release with a downloadable executable, and the first one shaped by
actually using the thing.

### Added

- **Configurable shortcuts.** *Settings → Shortcuts* lists all six with their
  current bindings; pick one, press the combination you want, Enter to save.
  A bare function key works — `F9` on its own is a valid binding — Delete
  unbinds a shortcut entirely, and Reset to Defaults puts all six back. The
  capture window unregisters the app's own hotkeys while it is open, because
  a global hotkey fires before the foreground window sees the key.
- **The menu's first row opens the repository.** A Win32 menu item cannot be
  a hyperlink, so it is an ordinary command that launches the browser.

### Changed

- **Default shortcuts are now `Ctrl+Shift+1`–`6`**, replacing `Alt+Shift`.
- **The shortcut column in the menu is read from the live bindings**, so a
  rebound shortcut appears there the next time the menu opens.
- **The icon was redesigned again** in a Material style: a gradient tile with
  anti-aliased corners, viewfinder brackets and three text lines at 24 px and
  above, two bold bars below that where the brackets would turn to mud.
- **Releases now carry the executable.** Tagging `v*` builds it, runs the
  tests, and publishes the binary with a SHA-256 checksum beside it. It is
  unsigned, so Windows SmartScreen will warn about it — see
  [the README](README.md#download).

### Fixed

- **The pointer did not change over the Record button.** The overlay set a
  move cursor everywhere inside the selection, including over the button. It
  now checks the button first, in the same order the click handler does, so
  what the pointer says matches what a click would do.

## [1.0.0] — 2026-09-25

First release. A C++/Win32 port of
[MacOS-menubar-SnipText-ProUltra](https://github.com/markpelayo/MacOS-menubar-SnipText-ProUltra),
which does the same three things from the macOS menu bar.

It builds clean under `/W4 /WX` and has been run on Windows 11 on one machine.
See [What has and has not been tested](README.md#what-has-and-has-not-been-tested)
for what that does and does not cover.

### Added

- **Screenshot** — region or full screen, opening an annotation editor with
  arrow, rectangle, ellipse, line, freehand pen and text tools. Every mark
  stays selectable, movable, resizable and restylable; undo covers all of
  those on the same footing as drawing, because it stores whole-array
  snapshots rather than a list of added shapes.
- **Screenshot to Text** — on-device OCR via `Windows.Media.Ocr`, with the
  geometry-driven line normaliser ported intact from the macOS version, to
  the clipboard.
- **Record Video** — a resizable region or a whole screen, to H.264 MP4 via
  Media Foundation, with optional microphone audio, selectable frame rate and
  quality, and mouse cursor and click capture. The recorder carries a
  generation number that every asynchronous entry point checks, plus watchdogs
  on the start and stop paths, so it cannot wedge and its finish callback
  fires exactly once per recording.
- **A recording indicator that is actually visible.** A green dashed frame
  around the recorded area, drawn strictly outside the captured rectangle so
  it never appears in the video, plus a `● 00:24  Stop` pill carrying the
  elapsed time that stops the recording in one click. A tray icon is added as
  a second way to stop — but not as the indicator, because Windows 11
  collapses the notification area behind a chevron by default.
- **A pinnable taskbar model**: the first launch stays resident to hold the
  hotkeys, later launches open the menu and exit. While idle there is no
  window, no tray icon, no timer and no background thread.
- Global hotkeys on `Ctrl+Shift+1` through `6`, in menu order.
- A menu whose first row names the program, its version and its author, so a
  screenshot of it identifies the build.
- Independent output folders for screenshots, text-capture sources and
  videos, each with its own submenu.
- **Sanitize and Restore Default** — saved files to the Recycle Bin, every
  setting back to its default, itemised in the confirmation with live counts
  and with Cancel as the default button.
- Run at Startup, with an optional delay that applies only to a launch
  Windows made after a boot, and a checkmark on the parent row whenever it is
  enabled.
- A plain-text log at `%LOCALAPPDATA%\SnipText\Logs\SnipText.log`, self-
  rotating at 512 KB.
- Twenty assertions for the text normaliser covering the six cases that
  shaped the algorithm and the list-marker traps it has to avoid.
- `build.bat` (MSVC, finds Visual Studio itself) and `CMakeLists.txt`, plus a
  GitHub Actions workflow that builds, tests and uploads both the executable
  and the build log on every push.

### Added for the 1.x shakedown

Both come out once the app is polished. They are marked as temporary at their
definitions, listed in [docs/RELEASING.md](docs/RELEASING.md), and reverting
is two edits.

- **Verbose logging is on by default.** A machine that has explicitly set
  `debugMode` still wins either way.
- **A startup environment block in the log** — app version and build stamp,
  Windows build number via `RtlGetVersion`, every monitor's bounds and
  scaling, the virtual-desktop rectangle, the process's DPI awareness,
  whether an OCR language is installed, and the three output folder paths. It
  is the context a bug report needs and nobody remembers to include.

### Known issues

- **Hotkeys are not configurable.** `Ctrl+Shift+<digit>` is unclaimed by
  Windows 11, but another program may already own one. It then silently does
  nothing for the session and the log names it; the menu item still works.
  Changing them is a one-line edit to the modifier mask in
  `RegisterHotkeys()`. Making them configurable is on the roadmap.
- On a full-screen recording the Stop pill has nowhere outside the captured
  area to sit, so it appears in the video. The log says so when it happens.
- Full-screen capture covers one monitor, the one under the pointer.
- Table columns merge into a single line. *Keep Line Breaks* preserves the
  rows.
- No webcam recording and no system-audio (loopback) recording. Microphone
  input works.
- Not code-signed. There is no binary download in this version; build it
  yourself. (1.1.0 attaches one, with the SmartScreen caveat that implies.)

### Notes on the port

The behavioural differences from the macOS original, and the reasoning behind
each, are listed in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#deviations-from-the-macos-original).
The short version: the app model, the confirmation surface, the recording
indicator, the hotkeys, the container format, the OCR engine and the editor's
Y axis all changed because the platform is different. Nothing else did.

[Unreleased]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/compare/v1.5.0...HEAD
[1.5.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.5.0
[1.4.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.4.0
[1.3.1]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.3.1
[1.1.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.1.0
[1.0.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.0.0
