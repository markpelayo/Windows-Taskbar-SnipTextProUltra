# Architecture

How the Windows port is put together, what changed from the macOS original and why, and the problems that turned out to be harder than they looked.

## Build model

No Visual Studio project file. `build.bat` compiles `src/*.cpp` with `cl.exe`, links statically, and embeds the icon and manifest. `CMakeLists.txt` does the same thing for people who prefer CMake. The whole build takes seconds and depends on nothing outside the Windows SDK.

**No C++/WinRT, deliberately.** The OCR engine is a WinRT type, and the obvious way to reach it is C++/WinRT — which means either a NuGet package or a `cppwinrt.exe` code-generation step in the build. Neither is compatible with "clone it and run `build.bat`". So `Ocr.cpp` uses the ABI headers (`windows.media.ocr.h`) with WRL's `ComPtr`, which ship in the SDK and need no generation. It is more verbose at exactly one call site and costs the project nothing.

## Files

| File | Role |
|---|---|
| `main.cpp` | Entry point: DPI declaration, COM, hand-off to `App` |
| `App.cpp` | The flyout menu, hotkeys, both capture pipelines, recording state |
| `framework.h` | Windows configuration macros and the RAII wrappers |
| `Util.cpp` | Strings, code points, paths, time, DPI, geometry |
| `Log.cpp` | Plain-text file logger with rotation |
| `Settings.cpp` | Registry-backed settings, and Run-at-Startup |
| `MediaFolder.cpp` | One output folder — three instances |
| `Bitmap.cpp` | 32-bit BGRA DIB section, PNG encoding, clipboard |
| `Capture.cpp` | Screen grabs and the shutter sound |
| `RegionOverlay.cpp` | The full-desktop selection overlay, in two styles |
| `Ocr.cpp` | `Windows.Media.Ocr` plus reading-order sort |
| `RecordingIndicator.cpp` | The green frame and the Stop pill shown while recording |
| `OcrLine.h` | One visual line: text plus bounding geometry |
| `TextNormalizer.cpp` | Raw OCR lines → pasteable text |
| `Clipboard.cpp` | Text clipboard write with read-back verification |
| `Annotation.cpp` | Shape model, drawing, hit-testing, resize geometry |
| `EditorWindow.cpp` | The editor: canvas, toolbars, selection, text entry, export |
| `EditorSettings.cpp` | Persisted tool, colour, stroke width |
| `VideoSettings.cpp` | Frame rate, quality, cursor/clicks, microphone |
| `ScreenRecorder.cpp` | Media Foundation sink writer and the recording state machine |
| `Toast.cpp` | The brief confirmation above the taskbar |

---

## The app model: what "taskbar icon" had to mean

The macOS original is `LSUIElement`: no Dock icon, no window, a menu bar item that owns a menu. Windows has no menu bar, so there were three candidates.

1. **A notification-area (tray) icon.** Closest in spirit, but you cannot pin it to the taskbar, and Windows 11 hides tray icons behind a chevron by default — so the icon the user asked for would usually be invisible.
2. **A visible window with a taskbar button.** Gets a taskbar button, but only while a window is showing. A utility that must keep a window open to be reachable is not the same utility.
3. **A pinned shortcut that relaunches.** What shipped.

Pinning on Windows pins a shortcut to the executable, so clicking a pinned icon launches the program. The first launch stays resident; every later launch finds the running instance through a named mutex plus `FindWindow`, posts it a registered window message, and exits. The running instance opens its menu.

That gives the user one icon that opens one menu, which is what they asked for, without keeping a window on screen or hiding in a chevron.

The one thing that genuinely needs a persistent visual is **recording**, and that gets a tray icon — created on start, destroyed on stop. It is a one-click Stop with no menu in the way, which is precisely the role the second status item plays on macOS.

Idle cost: a message loop, six hotkey registrations, and a zero-size `WS_EX_TOOLWINDOW` window that never appears in the taskbar or in Alt-Tab. No timer, no thread, no tray icon.

---

## DPI: the bug that only appears on someone else's monitor

The process declares **Per-Monitor-V2** awareness in the manifest *and* calls `SetProcessDpiAwarenessContext` before any window exists.

This is not cosmetic. Without it Windows virtualises coordinates: `GetCursorPos`, window rectangles and monitor bounds all come back in scaled logical units that do not match the pixels a `BitBlt` actually produces. A region selected on a 150% display then captures the wrong rectangle — and works perfectly on a 100% monitor, which is what makes it so easy to ship.

This is the direct Windows analogue of the `backingScaleFactor` crop bug the macOS version had to fix, and it has the same tell: correct on the developer's machine, wrong on the user's.

With Per-Monitor-V2 declared, **every coordinate the program handles is a physical pixel on every monitor**, and no scaling conversion appears anywhere in the capture or recording paths. The only place `DpiScaleForWindow` is used is for sizing chrome that should stay a constant physical size.

---

## The region overlay

One window covering the entire virtual desktop, painted with a **frozen screenshot of it**, taken before the window goes up.

Freezing first buys three things at once: the overlay can draw a dimmed backdrop with a bright hole in it, it can never appear in its own capture, and the screen underneath cannot change halfway through a selection. It is also how the Snipping Tool works, for the same reasons.

Two styles share the machinery:

- **Instant** — drag, release, done. Space switches to click-a-whole-window. Used by both Screenshot commands.
- **Adjustable** — a persistent rectangle with eight handles and a Record button; Enter confirms. Used by Record Region.

### It runs its own message loop

`RegionOverlay::Run` blocks until the user confirms or cancels. The alternative — returning to the app's loop and finishing in a callback — means every caller has to cope with a selection that is half made, which is where the macOS version's trickiest overlay bugs lived. A modal loop makes the caller read as straight-line code.

Two details in that loop:

- It exits on a `finished_` flag, woken by a posted `WM_NULL`. Posting `WM_QUIT` instead would leave a quit message in the thread queue that the app's own loop could later pick up and act on.
- `IsShowing()` stays true from the moment the overlay goes up until the `RegionOverlay` object is destroyed — which deliberately spans the gap between the window being hidden and the caller acting on the result. A second overlay raised inside that gap would end up in the first frames of the recording about to start.

### Only the changed rectangle repaints

`SetSelection` invalidates the **union of the old and new rectangles, grown by 90 pixels** — enough to cover the 2-pixel border, the handles, the size readout above and the controls below.

This view covers every monitor. Repainting a multi-megapixel composite on every mouse-move event is exactly what makes a selection feel sluggish, and mouse-move is when this runs. The paint itself goes through an off-screen buffer the size of the dirty rectangle, then one `BitBlt` to the screen.

### Window picking

Space toggles a mode where the window under the pointer is highlighted. Two details:

- The overlay hides itself for the duration of the `WindowFromPoint` call, because it is on top of everything and would otherwise always be the answer.
- Bounds come from `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`, **not** `GetWindowRect`. Since Windows 10 the latter includes the invisible resize border, so a window picked that way comes back with several dead pixels down each side.

### Clamping only when a new rectangle was begun

`didStartNewRect_` exists separately from "we are in drawing mode" for one reason: a click outside the selection that never moved must leave the previous selection untouched, while a perfectly straight vertical drag produces a zero-width rectangle that most needs the minimum size applied. Only the second one clamps.

The same reasoning is why the selection is **not** reset on mouse-down. Doing that meant one stray click outside the box replaced a carefully sized region with a 16-pixel square.

---

## The OCR pipeline

```
Alt+Shift+3  →  frozen desktop  →  crop  →  Windows.Media.Ocr  →  row bucketing
                                                 ↓
             clipboard  ←  TextNormalizer  ←  [OcrLine]
```

Recognition runs on a worker thread. On a full-screen capture it takes long enough that doing it on the UI thread would visibly freeze whatever the user was working in. The bitmap is released on that thread rather than handed back, because it is the largest allocation in the program and there is no reason to carry it across a thread hop.

### Reading order

Windows OCR returns lines already, unlike Vision's unordered observations — but it splits a visual row wherever the spacing is wide, table columns being the obvious case. So the same row-bucketing pass the macOS version applies to raw observations is applied here to the engine's lines, and the two ports agree on what a "line" is.

The sort uses **a single comparison key**, deliberately:

> A threshold-based comparator (`abs(a - b) < tolerance`, then compare X) is not a valid strict weak ordering — it isn't transitive. In Swift that produces garbage output. In C++ it is **undefined behaviour inside `std::sort`**, which can read out of bounds.

So the sort is `midY` descending and nothing else, and row grouping is a separate linear pass afterwards. `std::stable_sort` rather than `std::sort`, so exact ties are deterministic.

The bucketing tolerance compares against the row's **running mean** `midY`, not its first member — otherwise one tall heading widens the tolerance and swallows the lines beneath it. The mean is updated Welford-style with the count taken **after** the append; using the pre-append size is the classic porting bug and makes the mean drift toward later members.

### Coordinates

`OcrLine` holds normalised 0–1 coordinates with a **bottom-left origin**, matching the macOS original so the normaliser's constants port unchanged. Windows OCR reports top-left pixel rectangles, so the flip happens once, in `Ocr.cpp`, before any geometry is used. Doing it later would be meaningless: the bucketing tolerance is expressed in normalised height units.

Note the three different reductions in one struct — X is a union, height is a max, `midY` is a *mean*. A word with a descender must not drag the whole line's centre down.

### The engine's size limit

`OcrEngine.MaxImageDimension` is a hard ceiling, and a full-screen capture on a large display can exceed it. Refusing the capture would be worse than reading a downscaled copy, so the image is scaled to fit. Because all geometry is normalised, scaling changes nothing downstream.

### Text normalisation

OCR gives one entry per visual line. That is wrong for prose and right for code. The decision is made from **geometry**:

- A line that stops well short of the right margin didn't wrap, so its break is real
- An oversized vertical gap is a paragraph break
- Indentation and list markers (`•`, `-`, `1.`, `2)`) force a break

The constants, all in one place:

| Constant | Value | Relative to |
|---|---|---|
| Row-bucket tolerance | `0.5 ×` | `max(rowHeight, candidateHeight)` |
| Margin tolerance | `0.06 ×` | the text block's width |
| Leading threshold (break vs wrap) | `0.8 ×` | the median line height |
| Paragraph threshold (`\n\n` vs `\n`) | `1.6 ×` | the median line height |
| List-marker digit cap | 3 digits | — |

Three details worth keeping:

- The median uses **integer division** (`count / 2`), so for an even count it is the upper median, not the average of the two middle values.
- The right-margin test is `>=` and the indent test is `>`, using the same tolerance. The mixed strictness is intentional.
- The vertical gap can be **negative** when two boxes overlap. It is not clamped, because a negative gap correctly fails both thresholds.

The first implementation guessed from **line length** instead. It merged adjacent paragraphs into one and glued trailing labels like "Priority: High" onto the end of the preceding prose. `tests/TextNormalizerTests.cpp` holds the six cases that settled it — prose, two paragraphs, a UI dialog, a bulleted list, numbered steps, a hyphenated wrap — plus the traps the list-marker rule has to avoid: a bare dash is a hyphen, `2026.` is a year, `123.45` is a decimal.

### Why only `U+200B` is stripped

`CollapseWhitespace` deletes zero-width spaces and nothing else invisible, then splits on Unicode whitespace and rejoins with one space — which trims both ends and collapses the interior in a single pass. The whitespace and digit predicates go through `GetStringTypeW` rather than `<cctype>`, which is byte-oriented and would diverge on non-Latin input and on `U+00A0`.

### The clipboard is never clobbered on failure

Empty text is refused outright, before the clipboard is touched. OCR coming back blank must not destroy whatever the user already had copied. The write is then read straight back so the log can prove whether it stuck, and both `OpenClipboard` calls retry a few times, because any process can hold the clipboard for a moment.

---

## The annotation editor

Annotations are stored in **image pixel coordinates**, never in view coordinates. The same drawing code runs for the on-screen canvas (scaled) and the export (scale 1), so the saved PNG is full resolution and matches what was drawn regardless of the window size.

Unlike the macOS original this uses a **top-left origin** throughout, matching GDI+ and every other Windows coordinate in the program. The shapes are identical; only "up" changed.

### Selection and editing

Shapes are hit on their **outline**, not their interior. Clicking inside an empty rectangle starts a new drawing rather than selecting the rectangle, which is nearly always what you meant. Text is the one exception: a label has no meaningful outline to aim at, so its whole box counts.

Hit-test order matters: handles of the current selection first (they are small and sit on the outline), then the topmost annotation searched from the end, then empty space.

**Resize always computes from the rectangle as it was when the drag began**, never from the live one. Recomputing from the live rect makes the dragged edge chase the cursor once the shape flips through zero, collapsing it to a sliver — a bug that only shows up when you drag a handle past the opposite edge.

Undo is **whole-array snapshots**, not a list of added shapes, so moving, resizing, restyling and deleting are undoable on the same footing as drawing. Snapshots for a move or resize are taken on the **first actual drag event**, not on mouse-down — otherwise clicking around to select things stacks up identical undo states and Ctrl+Z appears broken. Restyles coalesce at 500 ms, so one slider drag is one undo step rather than forty.

### The arrowhead

`headLength = min(max(lineWidth * 4, 12) * scale, length)`. The 12-pixel floor is applied in **image units and only then scaled** — computing it in device units would make the head fatter on screen than in the exported file. The shaft stops at 80% of the head length so the point stays sharp.

### Keyboard commands, and why there is no accelerator table

Ctrl+Z, Ctrl+Y, Ctrl+C and Ctrl+S are handled in the canvas's `WM_KEYDOWN`, not through `TranslateAccelerator`.

That is the whole mechanism by which they stand down while a label is being typed. An accelerator table is consulted before the focused control sees the keystroke, so Ctrl+Z while typing would run the canvas's undo instead of the text control's, and Ctrl+C would copy the whole screenshot instead of the selected characters. Because the shortcuts live in the canvas's own procedure, the inline edit control simply never delivers them there.

Every toolbar action calls `ReturnFocusToCanvas()` afterwards, or focus stays on a button and Delete and Esc silently stop working on the selection.

### Text entry

The inline `EDIT` control is subclassed for two keys:

- **Enter** commits. Without this the control beeps.
- **Esc** must reach *cancel*, never the commit path. The default handling ends editing, which fires `EN_KILLFOCUS`, which **commits** the very label the user was trying to throw away.

The field's frame height is exactly `TextBoxHeight(fontSize) * scale` and its origin is exactly where the committed glyphs will be drawn. If any one of the three — field frame, `TextBoxHeight`, the draw box — changes, all three must, or the text jumps the moment you press Enter.

The colour is captured **when entry begins**, not read at commit time, so changing the swatch mid-typing doesn't commit the label in a colour it was never shown in.

`CommitTextEntry` clears its own active flag **first**, so the re-entrant call arriving from `EN_KILLFOCUS` is a harmless no-op.

### The colour popup

A transient popup that dismisses itself on deactivation — so by the time the swatch click arrives it has already gone, `colourPopup_` is null, and a naive toggle would immediately reopen it, making the swatch look inert. A 250 ms reopen guard fixes it.

The colour-wheel cell posts a message to the frame rather than opening the picker inline, for two reasons: the click handler is running inside the popup's own window procedure, which is about to be destroyed; and opening a modal dialog during a popup's dismissal hands focus back to the editor and leaves the dialog behind it.

### Export

Full resolution, always: `scale = 1, offset = 0`, with the identical drawing code the canvas uses. That is the payoff of the coordinate choice. The in-progress draft is excluded — a shape still under the mouse has not been committed.

The clipboard gets **both** `CF_DIBV5` and a registered `PNG` format. DIBV5 carries the alpha channel that plain `CF_DIB` does not; PNG is what modern applications prefer. Offering both means neither kind of consumer has to guess.

---

## Recording

Media Foundation's sink writer over an MP4 sink: H.264 video, AAC audio when a microphone is selected. Frames come from `BitBlt` on a worker thread, paced against wall-clock time rather than a fixed sleep, so a slow frame does not make the recording drift behind real time.

### The state machine cannot wedge

A recorder that can wedge is worse than one that occasionally fails. The failure mode is a timer counting up, a Stop that does nothing, and no way to start again short of quitting.

Three rules prevent it:

- **Every recording carries a generation number.** Every asynchronous entry point — both watchdogs, the worker's completion message — checks it before acting. `Teardown()` bumps it, so a late callback for a recording that has already been given up on is recognised as stale and dropped.
- **Watchdogs on both the start and the stop path**, ten seconds each. The start watchdog additionally checks `hasStarted_`, which the worker sets by posting a message once the sink writer is live.
- Because of the generation check, **the finish callback fires exactly once per recording.** Never twice — which is what an earlier token-free watchdog design would produce — and never zero times.

The watchdog window is deliberately generous. Setting up an encoder for a 4K region at 60 fps on a busy machine can genuinely take seconds, and the only cost of waiting is a later error message, where firing early kills a recording that was about to work.

`onStateChange` fires **before** `onFinish` on every path, because the state callback is what clears the recording indicator; firing it afterwards would wipe the "saved" confirmation in the same turn.

### Telling the user it is recording

The first version put a blinking icon in the notification area and nothing else, which is what the macOS original does with its menu bar. On Windows 11 that fails for a reason that has nothing to do with the code: **the notification area is collapsed behind a chevron by default**, so the indicator was invisible to the person it was for. They started a recording and had no way to tell it was running short of opening the menu.

The replacement is two windows, split by what each is for.

**The frame** says *what* is being recorded: a green dashed border around the region. It is sized to the region grown by the border thickness, and then the exact region is punched out of it with `SetWindowRgn`. What remains occupies only pixels **outside** the recorded rectangle, which buys two things at once — it cannot appear in the video, and it cannot cover the thing being recorded. It is `WS_EX_LAYERED | WS_EX_TRANSPARENT` and answers `HTTRANSPARENT`, so clicks pass straight through.

It is also painted exactly once. A static border costs nothing to keep on screen, which is why the dashes do not march.

The dashes are filled rectangles rather than a dashed pen: a pen's dash pattern is defined along the path, so drawing the four sides as one rectangle leaves the dashes meeting raggedly at the corners. The corners are drawn solid for the same reason.

**The pill** says *that* it is recording, and stops it: `● 00:24  Stop`, placed below the frame, or above it if there is no room below. Both of those are outside the recorded rectangle. A full-screen recording has no outside, so it goes in the bottom-left corner of the region and the log records that it will be in the video — an honest line beats a surprise.

Two details:

- The dot alternates **bright red and dim red**, never shown and hidden. A dot that vanishes reads as "stopped", and removing the glyph changes the text's width, which makes the whole pill jitter once a second.
- `WM_MOUSEACTIVATE` returns `MA_NOACTIVATE`. Clicking Stop must not pull focus away from whatever is being recorded, because a focus change is visible in the last frames of the video.

The ongoing cost of the whole thing is one repaint of roughly 150×34 pixels per second, driven by the recording timer that already existed. The tray icon stays as a second way to stop, but it is no longer the indicator.

### Encoding details worth knowing

- **Even dimensions.** H.264 with 4:2:0 chroma requires them, and after a 0.75 quality scale an odd result is easy to produce. Media Foundation will not round it for you.
- **Positive `MF_MT_DEFAULT_STRIDE`.** Our DIB sections are top-down. Leaving the stride unset gets a bottom-up interpretation and a vertically mirrored recording.
- **Quality is a scale factor, not a bitrate.** A scale factor is one number that cannot be subtly wrong; a hand-built encoder configuration can be, and fails at runtime rather than at build time. The menu labels say what it actually does — High is full resolution, Medium 75%, Low 50%.
- **Bitrate is proportional to the pixel rate**, clamped to a sane band. Hand-picked numbers go wrong at the extremes, and this has to work from a small snip at 15 fps to a 4K screen at 60.
- **The cursor and click highlight are drawn by hand.** Windows has no `capturesCursor` equivalent that composites for you, and no click highlighting at all.

### Audio is strictly best-effort

A device that has gone away, a mix format we cannot convert, an exclusive-mode conflict — none of these abort a recording. Losing the picture because the microphone was busy would be a much worse outcome than a silent video. The AAC encoder takes 16-bit PCM at 44.1 or 48 kHz in mono or stereo; anything else would need a resampler, and skipping audio is the honest answer rather than shipping a half-working conversion.

### Quitting mid-recording

`Finalize()` writes the MP4 `moov` atom — the direct analogue of the movie index AVFoundation writes on macOS. Skipping it leaves a file no player will open.

So the quit path stops the recording and then **pumps messages** in 50 ms slices for up to three seconds. It pumps rather than blocks because the worker's completion arrives as a posted message; a plain `WaitForSingleObject` would deadlock against the very thing it is waiting for.

---

## Settings, and why "restore default" removes rather than writes

macOS `UserDefaults` has registered defaults: a value that was never written reads as its default. That distinction is load-bearing here, so the registry layer reproduces it — every getter takes the fallback it should use when the value is absent, and `RestoreDefaults` **deletes** values rather than writing the defaults back.

The reason is `IsDefault()`, which the Sanitize menu item uses to decide whether there is anything to do. It compares **values, not key presence**. Key presence looked simpler and was wrong: the editor writes its tool, colour and width whenever a window opens or the user re-picks the tool they already had, so "a key exists" stopped meaning "the user changed something" — which left Sanitize permanently enabled. That is also why the three setters in `EditorWindow` each have an equality guard.

`debugMode` is deliberately excluded from Sanitize. It is a developer switch, not a setting the user chose.

### Sanitize uses the Recycle Bin

`SHFileOperation` with `FOF_ALLOWUNDO`, never `DeleteFile`. These are the user's own images and videos in their Pictures and Videos folders; a single menu click should not put them beyond recovery. The confirmation dialog itemises live counts, and **Cancel is the default button** — Enter should never be the key that throws a folder of screenshots away.

The folder enumeration filters on file extension, which is what stops Sanitize touching other files the user parked in the folder, and guarantees the dialog's numbers equal the menu's.

---

## Menu construction

The menu is rebuilt from scratch on every open.

Refreshing at the end of whichever action changed something was the original macOS approach and went wrong in a specific way: a screenshot wrote a PNG without refreshing the menu, so the saved-file count only moved when some unrelated action happened to trigger a rebuild. Rebuilding on display makes every count correct by construction.

Folder counts are enumerated **once** per rebuild and shared between the three "Show Saved" items and the Sanitize enablement, so the dialog's numbers and the menu's always agree.

Two labelling rules carried over:

- A folder row shows no path while it is at its default, because "Default" is the absence of information.
- The last-OCR preview is capped at 14 characters. It is the one label whose width varies with the user's data, and a generous cap would make the menu change width every time it was used.

---

## Startup delay detection

The Run at Startup delay applies only when Windows started the app after a boot — never to a launch the user asked for.

Detecting which is which is not as easy as it looks. It uses **system uptime**: startup entries fire moments after the desktop appears, so a launch inside the first two minutes of uptime is a startup launch and anything later is the user.

The bias is deliberate: when unsure it starts immediately, because a missing delay is invisible while a wrong one looks like a broken app.

This inherits a known inaccuracy from the macOS original, plus a Windows-specific one: fast startup and hibernate resume can leave the tick count high, so a genuine boot launch may read as manual and start without its delay. Starting too early is the failure mode this design prefers.

---

## Resource behaviour

Deliberate choices, since this process runs for weeks at a time.

- **Idle is genuinely idle.** No timer, no window, no tray icon, no background thread. The process exists to hold six hotkey registrations and a message loop.
- **One timer, only while recording**, at one tick a second, driving both the elapsed text and the blink so the two cannot drift apart.
- **Peak memory is one capture's bitmap** — about 8 MB for a 1440p screen, 33 MB for 4K — created late and released as soon as OCR or the editor is finished with it. Nothing is cached between captures.
- **Every GDI object, handle and COM pointer is owned by an RAII wrapper** from `framework.h`, so there is no branch — including an early return — on which a resource leaks.
- **Bitmaps release their memory DC explicitly.** The bitmap must come out of the DC before either is destroyed, or the DIB section stays alive for the process's lifetime.
- **Verbose log strings are never built** when verbose logging is off. `LOG_DEBUG` is a macro that short-circuits before evaluating its argument, and several debug call sites do real work.
- **Bounded by construction.** Undo caps at 50 snapshots; the log rotates at 512 KB, checked at launch and every 200th write.
- **Drag paths avoid full-resolution work.** The editor canvas drops to `COLORONCOLOR` resampling while a drag is in flight; freehand points closer than 1.5 view pixels are skipped, so a slow stroke doesn't accumulate tens of thousands of them. The mouse-up point is always appended, or every stroke ends short of where it was released.
- **One capture at a time.** A second hotkey press while a capture is running is ignored — otherwise you get two overlays and two clipboard writes racing.

---

## Deviations from the macOS original

Worth listing explicitly, since this is a port.

| macOS | Windows | Why |
|---|---|---|
| Menu bar item | Pinned taskbar shortcut, relaunch-to-open | Windows has no menu bar; see the app model above |
| Menubar title shows the char count | A small message above the taskbar for 1.6 s | Windows has no equivalent surface, and a notification balloon is heavier and permission-gated |
| `⌘⌥1`–`6` | `Alt+Shift+1`–`6` | Unclaimed by Windows 11; see the note below |
| `.mov` via AVFoundation | `.mp4` via Media Foundation | The native encoder on each platform |
| Vision OCR | `Windows.Media.Ocr` | The on-device engine each OS ships |
| Y-up coordinates in the editor | Y-down | GDI+ and every other Windows coordinate |
| Screen Recording permission (TCC) | — | Windows has no screen-capture permission gate, so the whole permission subsystem is gone |
| Stroke slider 1–20 | 1–40 | Stroke width is in image pixels, and on a 200% display the macOS range tops out too thin |
| Files to the Trash | Files to the Recycle Bin | Same idea, different name |
| Login item via `SMAppService` | `HKCU\...\Run` | The Windows equivalent |
| Legacy folder migration at launch | — | There is no earlier Windows version to migrate from |

### Choosing the hotkeys

Two combinations were ruled out before `Alt+Shift`:

- **`Win+Shift+<digit>`** sits next to `Win+Shift+S`, the built-in Snipping Tool. Too close to the thing this replaces.
- **`Win+Alt+<digit>`** is owned by the shell, which uses it to open the Jump List of the pinned taskbar app in that position — and it registers its hotkeys long before any user program starts. `RegisterHotKey` is first-come-first-served, so ours would simply lose.

`Alt+Shift+<digit>` is unclaimed by Windows 11 and wins neither of those races.

A third-party program can still own one. That failure is handled the way the macOS version handles a taken Carbon hotkey: the action is **not recorded** for a hotkey that was never installed, so the shortcut silently does nothing for the session and a log line names it. The menu item always works regardless, so the app is never unreachable.

`MOD_CONTROL | MOD_ALT` is the other unclaimed option and is a one-line change to the modifier mask in `RegisterHotkeys()`. Making the hotkeys configurable is the real fix and is on the list below.

---

## Possible future work

Capture history, table → TSV mode, capturing several monitors at once, a per-capture OCR language picker, configurable hotkeys, opt-in auto-paste, system-audio (loopback) recording, webcam picture-in-picture.
