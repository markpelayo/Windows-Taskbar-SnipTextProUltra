# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org).

## [Unreleased]

Nothing yet.

## [1.9.7] — 2026-09-30

A pre-release audit pass. Five bugs, one of them a regression from 1.9.6 and
two of them capable of losing work. See
[the release notes](docs/RELEASE-NOTES-v1.9.7.md).

### Fixed

- **Selecting Arrow no longer silently reverts to Rectangle.** A regression
  introduced by 1.9.6. `ToolKeyValue` writes Arrow through its `default:`
  arm, so `"arrow"` is a string `ToolFromKeyValue` has to parse — and it had
  no branch for it, getting away with that only because the fallback *used*
  to be Arrow. Moving the default to Rectangle made the missing case visible:
  pick Arrow, close the editor, open another, and you had Rectangle. Worse,
  `IsDefault()` then compared Rectangle to Rectangle and reported "at
  defaults" while a stale `"arrow"` sat in the registry — greying out the one
  menu item that would have cleared it.
- **Esc during a drag no longer closes the editor and discards everything.**
  A drag in flight was missing from the Esc cascade, and because mouse-down
  clears the selection before a Drawing drag starts, the selection rung could
  not catch it either — so Esc fell through to Close. Pressing Esc to back out
  of one crop marquee threw away every mark on the picture, with no prompt.
  Esc now cancels the drag, via `ReleaseCapture` so it takes the identical
  path an Alt+Tab mid-drag already took.
- **A tool shortcut can no longer be rebound onto `Ctrl+Z`, `Ctrl+Y`,
  `Ctrl+C` or `Ctrl+S`.** Those four are hard-coded in the editor and are not
  `hotkeys::Action`s, so the de-confliction pass could not see them. Binding
  Rectangle to `Ctrl+Z` meant the editor-local dispatch won — it runs from
  `PreTranslateMessage`, before the keystroke is dispatched at all — and undo
  was simply gone, with nothing said at bind time and no way back except
  Reset to Defaults. Now refused at capture time, because the other side of
  the collision is not a binding that can be moved. The check covers every
  key the editor handles directly — `Ctrl`(+`Shift`) with `Z`/`Y`/`C`/`S`/
  `Up`/`Down`, and bare `Delete`/`Backspace`/`F2`/arrows — and applies to the
  **global** shortcuts too, which is where it matters most: `RegisterHotKey`
  claims a combination system-wide and the keystroke is then dispatched to
  nobody, so a global on `Ctrl+Z` kills editor undo *and* takes the key from
  every other program on the machine.
- **A text label can no longer be placed in the grey letterbox**, where it
  became permanently unreachable: clipped out of both the canvas and the
  export, and excluded from hit-testing by `IsWithinCrop`, so it could not be
  selected, moved or deleted — yet it rode along in every snapshot and every
  save. Refused rather than clamped: a click on the mat around the picture is
  not a request to annotate the picture.
- **A failed recording or auto-save no longer leaves a broken file behind.**
  Only the zero-frame case deleted its output; a write error or a failed
  `Finalize` reported the error and left a partial, unplayable `.mp4` — which
  then counted toward the saved total in the menu and appeared in the
  Sanitize list, telling the user they had a recording they could not watch.
  Same fix for a truncated `.png` from `MediaFolder::SaveBytes`.

### Changed

- `docs/editor-toolbar.svg` / `.png` redrawn for the first time since 1.8.2:
  the current tool order, Rectangle shown selected, the `#007AFF` swatch,
  Crop in place of the long-removed Callout, the hint bar as it actually
  renders, and each button labelled with its number key. The disclaimer
  caption the stale figure needed is gone from both README and ARCHITECTURE.

## [1.9.6] — 2026-09-30

### Changed

- **The editor now opens with Rectangle selected, not Arrow.** It is the
  most-used tool, and after the 1.9.5 reorder it is also the first button and
  the `1` key — so the default, the leftmost button and the first digit are
  now the same tool rather than three separate arbitrary facts.
- **Crop disarms to the default tool** rather than specifically to Arrow, so
  "what Crop falls back to" and "what the editor opens with" stay one answer.
  Cropping is usually followed by annotating, so this also lands on the
  useful tool instead of the middle of the bar.
- The default tool is now a named constant, `kDefaultTool` in
  `Annotation.h`. It decides five things that have to agree — `currentTool_`'s
  initialiser, the string `CurrentTool()` falls back to, what
  `ToolFromKeyValue` returns for a value it cannot parse,
  `editor_settings::IsDefault`'s test, and the tool Crop disarms to — and
  they were five separate mentions of `Tool::Arrow`, which is four chances to
  move the default and miss one. A stored tool is unaffected: persistence is
  by name, so anyone who had picked a tool keeps it.

## [1.9.5] — 2026-09-30

Number keys for the tools, a reordered toolbar, a blue default, and the hint
line is now switchable. See [the release notes](docs/RELEASE-NOTES-v1.9.5.md).

### Added

- **Number-key shortcuts inside the editor.** `1`–`8` select the eight tools
  left to right, `9` toggles Keep the Editor on Top. All nine are rebindable
  under *Change Keyboard Shortcut → Only inside the editor*, below Esc.

  Bare digits are only defensible because these are **editor-local**: they
  are never handed to `RegisterHotKey`, so they take nothing away from any
  other program, and the editor's key handler stands down entirely while a
  text label is being typed — so typing "3 items" into a label does not
  switch tools.

  Both the tool and pin shortcuts dispatch by **sending the same `WM_COMMAND`
  the button click sends** rather than repeating what it does. Selecting a
  tool has to commit an open label and return focus; Pin has to write the
  setting the way the tray row writes it and tell every other open editor.
- **Tool button tooltips now name their key**, read from the live binding
  rather than written as a literal digit, and rebuilt when a binding changes.
  A tooltip naming last week's shortcut is worse than one naming none.
- **Show Tool Hints in the Editor**, a new tray menu row, on by default.
  Switches off the one-line modifier hint along the bottom of the canvas.

  A menu row rather than a toolbar button: the bar is eight tools and five
  commands, all of them things you *do*, and a preference that changes what
  the window says is a different kind of thing. It also costs no width on a
  bar whose minimum already sets the editor's minimum window size. Chosen
  over a wiki page because a page does not reduce clutter and nobody reads
  documentation mid-capture — though the information is now written down in
  three places regardless: the menu, the tooltips, and the README.

### Changed

- **The default annotation colour is blue (#007AFF), was green (#34C759).**
  Already one of the nine swatch presets, so the grid shows the default as
  selected rather than as a tenth colour nobody picked. Deliberately *not*
  the system accent the selection chrome uses: they look alike, which is the
  point, but a mark drawn in exactly the selection colour would be hard to
  tell from its own dashed rectangle and handles.
- **Toolbar order is now Rectangle, Ellipse, Arrow, Line, Pen, Text, Lift,
  Crop.** The two shapes people reach for most are now first, next to the
  slider that sizes them.

  Done by reordering the `Tool` enum rather than adding a display-order
  array, which makes enum order, toolbar order and digit order one list
  instead of three. Safe because nothing stores a tool as a number — the
  persisted setting is a string, button ids are rebuilt every launch, and
  every switch is by name.
- Four copies of the default colour across three headers collapsed into one
  definition, in `Annotation.h`, where the default colour of an Annotation
  belongs.

### Fixed

- **A menu command id collision that would have shipped.**
  `ID_SHORTCUT_BASE` was 1080 with room for the seven actions of the day.
  Going to sixteen ran it to 1095 — straight through
  `ID_ENGINE_AUTO`/`WINDOWS`/`TESSERACT` at 1090–1092. Three of the new
  shortcut rows would have silently changed the OCR engine, and picking an
  OCR engine would have opened a rebind dialog. Nothing would have warned:
  menu command ids are plain `int`s and a collision is just two names for
  one number. Moved to 1300, clear of every other block, with a
  `static_assert` so the next one fails at compile time instead.
- `IsGlobal` was `action != CloseEditor` — a list of exceptions. Adding nine
  editor-only actions to that would have registered **bare 1–9 as
  system-wide hotkeys**, taking those keys from every program on the
  machine. Restated as a threshold, which cannot be forgotten when the next
  editor-only action is appended.
- **Pressing Esc to cancel a tool rebind would have bound Esc to that tool
  and silently destroyed Esc-to-close.** The rebind window's Esc-cancels
  gate was `IsGlobal(action)`, correct only while `CloseEditor` was the sole
  local action; the `IsGlobal` rewrite above widened that exception to all
  ten. Esc would have been recorded as the candidate binding, Enter would
  have committed it, and the de-confliction pass would then have found
  `CloseEditor` holding Esc *in the same scope* and unbound it — so trying
  to back out of a rebind would have permanently removed Esc-to-close,
  without a word. Now tested against `CloseEditor` by name, which is the
  only action the exception was ever for. The footer text uses the same
  condition, because it is the only warning the window gives.
- `hotkeys::kToolActionCount == kToolCount` is now a `static_assert`. It was
  declared and documented as the invariant holding the two enums together,
  and enforced nowhere: a ninth tool would have got no shortcut, no
  dispatch, and `SelectTool1 + 8` — which is `TogglePin` — printing Pin's
  key in its tooltip. A wrong but plausible string is worse than a crash.
- `docs/ARCHITECTURE.md`'s section on non-global shortcuts described one
  action, `WM_KEYDOWN` instead of `PreTranslateMessage`, and contradicted
  itself two paragraphs later. Rewritten, and the number keys, the toolbar
  order invariant and the hint setting are now documented there.

## [1.9.4] — 2026-09-30

Feedback for the keyboard shortcuts. See
[the release notes](docs/RELEASE-NOTES-v1.9.4.md).

### Added

- **`Ctrl+Z`, `Ctrl+Y`, `Ctrl+C` and `Ctrl+S` now flash the matching toolbar
  button**, so a shortcut says *which* command it ran rather than leaving you
  to infer it from the canvas. Undo and redo are the pair that needed this:
  adjacent keys with opposite effects, and on a drawing with few marks the
  result of hitting the wrong one can be genuinely hard to see.

  Implemented with `BM_SETSTATE` rather than a separate "pretend pressed"
  flag, so the fake press travels the same `ODS_SELECTED` path through
  `WM_DRAWITEM` as a real click. 150ms — short enough not to look stuck, long
  enough not to be missed between saccades.

  A **disabled** button does not flash, and neither does anything while a
  label is being typed. `Ctrl+Z` on an empty undo stack stays silent, because
  "nothing happened" is the truthful answer and is itself worth knowing. This
  is also why the flash is raised *before* the command runs rather than
  after: the button's enabled state still describes whether the command is
  about to do anything, which afterwards it may not — the last undo disables
  Undo. Only Undo and Redo are ever disabled, so Copy and Save always flash;
  they report success or failure in the title bar instead.

  Holding a shortcut down auto-repeats, and each repeat restarts the release
  timer rather than stacking one, so the button stays down for the whole run
  of undos and comes back up once.

### Fixed

- **A disabled toolbar button could draw with the blue "switched on" face.**
  Found by audit. Sharing `BM_SETSTATE` with a real click shares the
  mechanism but not the reachable states: a real click releases the button
  before `WM_COMMAND`, so the `EnableWindow(FALSE)` that follows the last
  undo always landed on a released button. The shortcut flash holds it down
  across that disable, producing pressed-and-disabled — the selected-tool
  blue with a greyed glyph, a combination that appears nowhere else in the
  UI, on precisely the case the feature was built for. The face now ignores
  `active` and `pressed` when the button is disabled, so a spent Undo simply
  goes grey, which is the honest signal anyway.
- Two stale comments corrected: `WM_TIMER` claimed `baseTitle_` was captured
  once at construction (`UpdateTitleForCrop` rewrites it — which is exactly
  why the revert survives a crop landing mid-flash), and `WM_CREATE`
  promised Save an accent ring that was never wired up.

### Changed

- **The title-bar confirmation now lasts 2 seconds, up from 1.2.** 1.2s was
  long enough to notice that something had changed and too short to read the
  word, which is the worst of both.

## [1.9.3] — 2026-09-30

Shift now constrains the geometry on **every** tool that has geometry, with no
exceptions left, and Lift's modifiers were rearranged to match the operating
system. See [the release notes](docs/RELEASE-NOTES-v1.9.3.md).

### Changed

- **Lift: plain drag now MOVES instead of copying.** This is a deliberate
  inversion of shipped behaviour. Dragging blanks the source and leaves a
  patch; **Ctrl**+drag copies and leaves the original in place. Previously
  plain drag copied and **Shift**+drag cut.

  Three reasons, in order of weight. Shift had to come free so it could mean
  "constrain" on Lift as it does everywhere else. `Ctrl`-drag-to-copy is what
  File Explorer and every other drag on Windows already does, so this borrows
  existing muscle memory instead of competing with it. And plain dragging
  *moves* things — leaving a duplicate behind was the surprising default, even
  though it was the safer one. The lost safety is answered by `Ctrl+Z`, which
  already undid a lift in one step; the capture underneath is still never
  modified, so nothing is destroyed either way.
- Shift's meaning is now stated as two predicates in `Annotation.h`,
  `ToolSnapsToAxis` and `ToolConstrainsToSquare`, whose union is exactly the
  set of tools that get a canvas hint. Ctrl's is `ToolCanFill` and
  `ToolCopiesWithCtrl`. `ToolIsClosedShape` was doing double duty for
  squaring and filling; those two sets have genuinely diverged, so it is gone.
- Canvas hints for Lift and Crop rewritten. Every hint line now names Shift
  first and in the same position, because it means the same thing on all six
  tools that show one — a hint bar that reads the same way every time teaches
  the rule rather than the line.

### Added

- **Shift constrains Lift's selection to a perfect square.**
- **Shift constrains Crop's selection to a perfect square**, squared in image
  pixels after rounding rather than in screen pixels, so it is exactly square
  at any zoom and stays square if you measure it.

### Fixed

- A Shift-squared Lift or Crop selection that ran into the grey letterbox
  could come back as a rectangle. Both tools clamp their region — Lift to the
  current crop, Crop to itself — and the clamp knew nothing about squareness,
  so the marquee showed a square and the result was not one. Found while
  adding the feature rather than by hitting it: the clamp is invisible unless
  the drag leaves the picture, which would have made this rare, intermittent
  and very hard to attribute. `ClampRegion` now takes a `keepSquare` flag and
  **shrinks to the shorter side** — growing to the longer one would push the
  region back outside the bounds the clamp exists to enforce — and it takes
  the **drag anchor**, so it trims the edges away from the corner being held
  still. Shrinking toward the top-left regardless, as the first version did,
  is correct only for a drag that went down and to the right; the other three
  quadrants got a correctly-square selection in the wrong place. Both
  Shift-drag callers clamp before `ApplyCrop`, whose own integer squaring is
  then only a sub-pixel rounding tidy-up.

## [1.9.2] — 2026-09-30

### Added

- **Shift gives you a perfect square or a perfect circle.** Hold it while
  dragging a Rectangle or an Ellipse and the shape is constrained to 1:1.
  Read live, so it squares up the moment Shift goes down and springs back
  when it comes up, without releasing the button.

  The side is the *larger* of the two spans, so the shape grows to contain
  the drag rather than shrinking to fit inside it — it keeps up with the
  pointer instead of lagging behind the dominant axis.

### Changed

- **Filling a shape moved from Shift to Ctrl.** Shift was taken, and it had
  to be: constraining proportions is what Shift means in every drawing
  application, and filling is not a constraint. Keeping that convention
  intact is worth more than the one gesture.

  | | drag | Shift | Ctrl | Ctrl+Shift |
  |---|---|---|---|---|
  | **Rectangle** | outline | square | filled | filled square |
  | **Ellipse** | outline | circle | filled | filled circle |

  Two independent switches rather than four behaviours: Shift constrains,
  Ctrl fills. That is why the fourth column needs no explanation of its own.

  Ctrl-drag was free — Ctrl+*arrows* is layering, and that is the keyboard.

- **Ctrl shows the fill live in the preview**, which Shift never did: the
  old fill was only applied at mouse-up, so the preview was always an
  outline.

  The modifier keys drive the preview themselves, on key-down and key-up as
  well as on mouse-move. Reading them only while the pointer moves left a
  hole: press Shift, then release the button without moving, and the last
  frame drawn was a square while the mark committed was a rectangle. All
  three paths now call one resolver, so preview and commit agree by
  construction.

- Lift keeps Shift for cutting. It is the one place Shift is not a
  constraint, and it has its own hint line rather than surprising anyone
  silently.

## [1.9.1] — 2026-09-30

### Fixed

- **Nothing could be drawn.** Every completed drag was discarded: no arrow,
  no rectangle, no ellipse, no line, no pen stroke, no lifted piece — and
  Crop drew its marquee and then did not crop. The preview during the drag
  was correct, so the tools looked alive right up to the moment you let go.

  1.9.0 added a `WM_CAPTURECHANGED` handler to the canvas, so that a drag
  interrupted by Alt-Tab or a lock screen would not leave the mark following
  the pointer afterwards. The handler resets `dragMode_` to `None`.

  `ReleaseCapture` **sends** `WM_CAPTURECHANGED` synchronously, and
  `WM_LBUTTONUP` called it on its first line. So by the time mouse-up got
  round to asking what kind of drag had just finished, the answer had
  already been overwritten — every drag looked like a completed *move*,
  which has nothing to commit, and took that path.

  `WM_LBUTTONUP` now reads the mode into a local **before** releasing the
  capture and uses that for the rest of the handler, so a reset arriving
  from inside `ReleaseCapture` cannot cancel a drag that is in the middle of
  being committed.

  The handler now also abandons the in-progress draft, which is what it
  should have done for a genuine interruption in the first place.

## [1.9.0] — 2026-09-30

A minor version, because a label stopped being a kind of mark and became a
property of every mark — and the Callout tool went with it.

### Added

- **Any mark can carry a label.** A rectangle, an ellipse, a line, an arrow,
  a pen stroke, a lifted piece of the picture. The label is drawn beside the
  mark with a short leader line pointing back at it, and the three together
  are **one object**: select it, move it, restyle it, delete it, undo it as a
  unit.

  **Double-click a mark to label it**, or press **F2** with it selected.
  Double-click is the discoverable one, F2 is the Windows rename convention.
  On a Text mark the same key edits the text itself, because that is the same
  operation — both are the string you typed.

  **Drag the label to swing it** around its mark. It snaps to one of eight
  directions — the four sides and the four diagonals — and the leader follows.
  Always snapped, so there is **no modifier to hold**: the position is stored
  as an angle *index* plus a gap rather than as a free point.

  That choice pays for itself twice. The label re-derives its position from
  the mark's current bounds every time it is drawn, so resizing a rectangle
  carries its label along and the leader can never end up pointing at
  nothing. And an empty label is how you take one off again.

### Removed

- **The Callout tool.** It was an arrow that carried a label, and a label
  turned out not to be a *kind* of mark. An arrow with a label is exactly
  what Callout was, so nothing is lost — there is one fewer tool to explain,
  eight instead of nine, and the minimum window width goes back to 502px.

  A saved tool of `callout` falls back to Arrow, which is what it now is.

### Changed

- The modifier budget is untouched. Shift still means fill on the closed
  shapes, cut on Lift, and snap-to-45° on Line and Arrow. Labels needed no
  modifier at all, which is why they are not on Ctrl-drag — and why Ctrl
  keeps meaning layering.

## [1.8.6] — 2026-09-30

### Fixed

- **The build.** A local `kPi` inside the new snapping helper shadowed the
  file-scope one added for the undo glyph, which is C4459 — and `/WX` makes
  that a build failure. Caught by CI rather than by review, because the
  review step was skipped for this release.

### Added

- **Shift snaps Line and Arrow to 45°.** Hold Shift while drawing and the
  line locks to the nearest of the eight rays — horizontal, vertical and
  both diagonals, in every direction. Read live, so it straightens the
  moment Shift goes down and springs back when it comes up, without
  releasing the button. It applies when re-aiming an existing line by either
  of its end handles too, since that is the same gesture.

  By projection rather than rotation: the snapped end is where the cursor
  falls perpendicular onto the ray, so it stays beside the pointer instead
  of swinging away at a fixed radius. Dragging roughly east gives an end
  that tracks the cursor horizontally with its height pinned, which is what
  the gesture is expected to feel like.

  Line and Arrow only, as asked. Pen is freehand by definition, and the
  closed shapes already use Shift for filling.

- **Layering, on Ctrl+Up and Ctrl+Down.** Bring the selected mark forward or
  send it back one step; add Shift to send it all the way. Everything in the
  editor is an object, including a lifted piece of the picture, so anything
  can be put in front of or behind anything else.

- **Arrow keys nudge the selection** — one image pixel, ten with Shift.
  Image pixels rather than view pixels, so a nudge on a capture shown at
  half size moves the mark one pixel in the *file*.

### Changed

- The canvas hint now covers Line and Arrow, since they have a Shift
  variant: *Drag to draw · Shift-drag to snap to 45°*.

## [1.8.5] — 2026-09-30

### Fixed

- **The slider and the colour picker flickered.** Not animation — there is
  none anywhere in this program, and none was added. It was the compositor
  showing a half-finished paint.

  Every one of those surfaces was painted in layers straight to the screen.
  The slider filled its background, then the track, then the travelled part,
  then the thumb — four passes over the same pixels, and the eye catches the
  intermediate states as a flash. The picker drew ten cells, a six-wedge
  wheel and eleven outlines the same way.

  They now draw into an off-screen bitmap and blit it once. That is
  **strictly less work than before**: the overlapping fills happen in memory
  where nothing has to be composited, and the screen is touched exactly once
  per paint instead of once per layer.

- **Owner-drawn controls no longer erase before they paint.** Every one of
  them covers its whole rectangle, so the erase pass was a full-control fill
  the user could see, immediately overdrawn. `InvalidateRect` now asks for a
  repaint without an erase, and the slider and picker refuse `WM_ERASEBKGND`
  outright.

## [1.8.4] — 2026-09-30

### Fixed

- **The slider's thumb sat above its track.** Not a styling quirk: a
  horizontal comctl32 trackbar without `TBS_BOTH` gets a downward-*pointing*
  thumb, and Windows makes room for the point by pushing the channel above
  centre. It is centred now, because it is no longer a trackbar.

### Changed

- **The width slider is drawn by the editor**, not by comctl32. A trackbar's
  thumb shape belongs to the system and cannot be restyled, so matching the
  rest of the bar meant owning it: a rounded track, the travelled part in
  the accent colour, and a round white thumb with an accent ring.

  It holds no value of its own. `currentLineWidth_` is the single copy and
  the slider reads and writes that directly, so there is no `TBM_SETPOS`
  round trip and no way for the control and the editor to disagree. It also
  takes the mouse wheel — forwarded from the canvas when Windows' "scroll
  inactive windows" setting is off, since a wheel message goes to the
  focused window and the slider never takes focus.

  It gives up one thing the trackbar had: the MSAA/UIA interface it exposed
  to screen readers. Nothing else in the program implements one either, so
  it is consistent rather than newly broken, but it is a door closing.

- **Every button on both bars has antialiased rounded corners.** Drawn with
  GDI+ rather than GDI, because GDI does not antialias and a GDI rounded
  corner is a staircase — visibly worse than the square corner it replaces.
  GDI+ was already linked and already initialised for annotations, so this
  is a different drawing call rather than a new dependency.

  **No measurable cost.** The toolbar is not in any hot path: `WM_DRAWITEM`
  fires only when a button is invalidated, and the thing that runs on every
  mouse-move is the canvas, which is untouched.

  **One real cost, stated plainly:** hand-drawn chrome does not follow
  Windows. No dark mode, no high-contrast mode, no accent-colour follow. The
  buttons gave that up in 1.8.0 when they became owner-drawn; the slider was
  the last control the system still themed, and it gives it up here.

## [1.8.3] — 2026-09-30

### Added

- **Crop.** A ninth tool. Select it, drag a rectangle, and that region is
  what the editor shows and what Copy and Save produce.

  **Marks survive it.** A mark that straddles the new edge is clipped where
  the picture is, and stays selectable, movable and deletable. One that
  falls entirely outside still exists — it is not visible, but undoing the
  crop brings it back exactly where it was.

  **Ctrl+Z undoes a crop**, the same as it undoes a mark, because a crop is
  an edit rather than a mode.

  It is a one-shot: the tool reverts to Arrow once the crop is applied,
  since leaving it armed means the next drag silently crops again.

  Nothing is destroyed. The crop is stored as a **rectangle** rather than by
  cutting down the capture — the capture is never modified at all, which is
  what makes undo work and what keeps the undo stack cheap. A rectangle
  costs sixteen bytes per step; a bitmap would cost 33 MB for a 4K capture,
  and fifty of those is a gigabyte and a half of undo history for a
  screenshot editor.

  Marks keep the coordinates they were drawn in, so repeated crops cannot
  accumulate an offset, and there is exactly one place — `ToImagePoint` /
  `ToViewPoint` — where the crop enters the coordinate system.

  The window title follows the crop, so it reports what you would get if you
  saved.

## [1.8.2] — 2026-09-30

### Added

- **Esc closes the editor**, and it is rebindable. A new row in Change
  Keyboard Shortcut, under a *Only inside the editor* heading, because it is
  the first shortcut in the program that is **not** global — the six above it
  are claimed from the whole system with `RegisterHotKey`, and doing that to
  a bare Esc would take the key away from every other program on the machine.
  This one is matched by the editor itself.

  It works whenever the editor is the **active window**, whatever has focus
  inside it. That takes a `PreTranslateMessage` hook in the message loop
  rather than a handler in a window procedure: a key only ever reaches the
  control with focus, and an editor is a frame full of controls, so handling
  it in the canvas meant Esc worked on the canvas and stopped working the
  moment you clicked a tool button. Reaching the hook at all means the
  message is bound for this editor's window tree, which for keyboard input
  is the same statement as "this editor is in front".

  Esc is a cascade, not a single meaning: it cancels an in-progress label
  first, then clears the selection, and only closes the window when there is
  neither. So a mistyped label costs one press, not the editor. While a
  label is open the hook stands aside for every key, not only Esc — this
  is the one action a bare letter can be bound to, and a narrower guard
  would let such a binding close the editor mid-word. The colour picker
  handles Esc itself, dismissing the picker first.

  No save prompt. Nothing has been written to disk, Copy and Save are one
  keystroke each, and a confirmation on a scratch window is the kind of
  dialog people learn to dismiss without reading. Unbind it if that is not
  the trade you want.

- **The pointer now says what a click will do.** The canvas showed a
  crosshair over everything, including the marks you were trying to grab.

  | Where the pointer is | Cursor |
  |---|---|
  | A corner handle | diagonal resize, matching the corner |
  | A side or top/bottom handle | horizontal or vertical resize |
  | A line or arrow endpoint | move — an endpoint is not constrained to an axis |
  | Any mark that would be picked up | move |
  | Empty canvas, Text tool | I-beam |
  | Empty canvas, anything else | crosshair |

  Resolved in the same order `WM_LBUTTONDOWN` resolves a click, so the
  cursor is a promise about what clicking will do rather than a decoration —
  any other order and it would be lying at the boundaries. Mid-drag the
  answer is frozen, so a gesture does not change its mind because the
  pointer wandered over something else on the way.

### Fixed

- **The colour wheel spilled out of the colour picker.** The tenth cell —
  the one that opens the system picker — was drawn with a radius of a whole
  cell rather than half of one, and nothing clipped it, so the wheel
  overflowed its square by 16px in every direction. That square is the last
  column of the bottom row, so the overflow left the popup entirely and sat
  on the toolbar and the canvas. It has been wrong since the picker was
  written.

- **The picker opens upwards again.** 1.8.1 moved it below the swatch, out
  of the window, on the theory that rising into the canvas was what made it
  cover the picture. It was not — the escaping wheel above was. A swatch on
  the bottom bar opens upwards, the way every other bottom-anchored menu on
  Windows does. It still flips the other way if the work area has no room.

## [1.8.1] — 2026-09-30

Four corrections to 1.8.0, three of which are features that were built to
answer the wrong question.

### Changed

- **Pin to Screen now keeps the EDITOR on top**, and is renamed *Keep the
  Editor on Top*. The first version floated a separate borderless copy of
  the capture with no toolbar on it, which is not what pinning is for — the
  window worth keeping in front of you is the one you are annotating in.

  It applies to every editor, so it covers full-screen captures too. The
  original objection (a full-screen pin would cover the thing it is a
  picture of) was wrong: the editor scales its capture down to fit, so a
  pinned full-screen shot is a window like any other.

  Toggling it — from the tray row or the toolbar button — raises or lowers
  every open editor immediately, rather than applying to the next one.

  While it is on, the editor is excluded from every capture the program
  takes — a topmost window is always in the way, and the one thing you
  cannot do about this one is move it aside. Turning the setting off puts
  it back into captures.

  The `PinnedWindow` class is gone, and with it about 550 lines, a window
  class, a registry of live pins and a second reap path through `App`.

- **Redact is no longer a tool. Shift-drag fills a Rectangle or an Ellipse**
  in the colour you picked, exactly the way Shift-drag turns a Lift from a
  copy into a cut. A tool whose only difference from Rectangle was the
  brush had not earned a slot on the bar, and the canvas now shows the
  modifier hint for all three tools that have one.

  Eight tools instead of nine, and the swatch is back to meaning one thing:
  Redact had forced a second hidden colour behind it, defaulting to black.

  The reason a redaction must be a *flat fill* rather than pixelation or
  blur has not changed and is kept in the header where someone will read
  it.

- **The callout's label moved to the arrow's tail.** It sat past the
  arrowhead — on top of the very thing the arrow was drawn to single out.
  It now sits behind the tail, in the empty space the drag started from, so
  the arrow leaves the text and travels to the subject. The icon was
  redrawn the same way round: the letter first, the arrow leaving it.

### Fixed

- **The colour picker covered the picture.** It opened upwards from the
  swatch, and because the swatch is on the bottom bar, "upwards" is always
  over the canvas — over the capture, in the corner, while you choose the
  colour you are about to draw on it with.

  It opens **downwards** now, outside the window entirely, which is also
  the direction the swatch's caret has been claiming since it became
  owner-drawn. It flips back up only when the monitor's work area has no
  room below, so covering the canvas is the fallback rather than the rule.

  v1.7.7 moved the Lift *hint* out of the picker's way, which was a real
  collision but not this one.

## [1.8.0] — 2026-09-30

A minor version rather than a patch: two new tools, a new window, and the
editor's toolbar rebuilt around them.

### Added

- **Pin to Screen.** A new tray row directly below Auto-Save Images. With it
  on, **Screen Capture a Region** sticks the capture to the screen in a
  floating, always-on-top window instead of opening the editor — so an error
  message, a part number or a diagram stays visible while you work in
  something else.

  Region only, deliberately: a full-screen capture pinned on top of the
  screen would cover the thing it is a picture of.

  The pin opens exactly over the region it was cut from, so it appears to
  lift off the screen in place rather than materialising somewhere else and
  making you find it. Drag anywhere on it to move it. Scroll to zoom, around
  the pointer, so the pixel under the cursor stays under the cursor.
  Double-click to hand it to the editor — the editor gets a copy, so closing
  the pin afterwards takes nothing away. Right-click for Open in Editor,
  Copy, Save, Actual Size, Close Every Other Pin and Close. Esc closes the
  focused pin, which is the one you last clicked.

  It replaces the editor, not the rest of the pipeline: Auto-Save still
  writes to disk, and Copy to Clipboard and Close still copies. Opening the
  editor *and* floating a copy of the same picture would be two answers to
  one question. There is a count in the tray menu and a row to close them
  all when any are up.

- **Redact.** A new tool: drag a rectangle, and it fills solid and opaque.
  Black by default — the swatch changes it while Redact is selected, which
  is useful for matching a background, but that has to be a decision rather
  than a default.

  It is a flat fill and nothing cleverer, on purpose. Pixelation and blur
  both *look* like protection while leaving the original recoverable: a
  screenshot has a known font at a known size, so the attack is to render
  candidate text, pixelate it on the same grid and compare — forwards, not
  backwards, one glyph at a time, which is why published tooling has been
  reading pixelated text since 2022. A flat fill is the only version whose
  output does not depend on the pixels underneath.

  It is an annotation like any other, so it moves, resizes and undoes, and
  the capture underneath is never modified until you export.

- **Callout.** A new tool: drag an arrow, then type a label that sits at its
  tip. One annotation rather than an arrow plus a separate text mark, so
  moving it moves both halves and the label cannot be left behind pointing
  at nothing. The label goes on the far side of the head, flipping to the
  left when the arrow points left, so it never covers the thing being
  pointed at. Esc during typing keeps the arrow and drops the words.

### Changed

- **The editor toolbar is rebuilt, and everything on it is now an icon.**
  Nine tools along the bottom; Undo and Redo anchored left, the Pin toggle
  centred, Copy and Save anchored right along the top. Every one has a
  tooltip. Reading the top row left to right: what you did, what will
  happen next, where it goes.

  All fourteen icon buttons are 34 × 28 and drawn by one function, so the
  two bars cannot drift apart. (The colour swatch is the one exception at
  44 × 28, because it shows a colour rather than a glyph.) Switched-on buttons — the selected tool, and Pin
  when enabled — take a filled face, a doubled accent ring and accent-
  coloured ink; that is the only state which survives letting go of the
  mouse, so it is the only one worth marking. Save no longer carries a permanent ring of its own, which
  was the last thing making the top row look like a different toolbar.

  Three groups, three anchors, so they are positioned independently and
  widening the window only grows the gaps between them. They can only meet
  by the window getting too narrow, and that is a number rather than a
  z-order — `34 + (82 + 12) × 2 = 222`, which `WM_GETMINMAXINFO` will not
  let you cross.

  222 is far below the tool row's 540, so for the first time since the
  editor was written the **bottom** row sets the floor. The minimum window
  width goes from 700px, to 678px with a text command group, to **540px**
  now — narrower than it has ever been, with two more tools than it has
  ever had.

  Every glyph is drawn in GDI from lines, arcs and Béziers. There is no
  image resource anywhere in this program and adding one for this would
  have been the first.

- **Pin to Screen can be switched from the editor.** It was going to be a
  read-only indicator; it is a real toggle in the centre of the top bar,
  writing the same registry value the tray row writes. Toggling either one
  repaints the other, and every other open editor, so two windows can never
  disagree about one setting.

  Icon-only, like everything else on the bar, so the whole weight of "is
  this on?" falls on the button's appearance — switched on it takes the
  same filled face and accent ring a selected tool takes. Its tooltip
  spells the state out in words and is rewritten on every toggle, because
  with no label on the face a stale tooltip would be the only thing on
  screen contradicting the button.

## [1.7.7] — 2026-09-30

### Fixed

- **The Lift hint collided with the colour picker.** Select Lift, then open
  the colour swatch, and the picker covered the start of *Drag to copy a
  piece · Shift-drag to cut it out*.

  Two things anchored to the same corner. The picker is 204 × 90 and opens
  directly above the swatch — the leftmost control on the bottom bar — so it
  rises into the bottom-left of the canvas. The hint was centred 12px above
  the canvas bottom, which is the same band. At the 700px minimum window
  width the hint spanned x 207–492 against a picker spanning x 10–214: seven
  pixels of overlap, and the picker is a topmost window, so it won.

  The hint is now right-aligned. Anchoring the two to **opposite** edges
  makes the clearance a property of the layout rather than a coincidence
  that happened to hold at the window sizes anyone tried — at the minimum
  width the hint starts at x 403 against a picker ending at 214, and
  widening the window only adds more. It also puts the hint under the tool
  buttons, which is where the click that summoned it happened.

## [1.7.6] — 2026-09-29

### Fixed

- **The selection border tore while dragging.** Pulling out a region left the
  border with a horizontal jog in it, near the pointer — the line ran straight
  down, stepped sideways by however far the mouse had moved, and carried on.

  It was not jagged drawing. It was half a frame. The overlay is a
  full-desktop, topmost, opaque window repainted on every mouse-move — a blit
  straight into the window's surface at whatever rate the mouse reports, which
  on a fast mouse is several hundred times a second. DWM copies that surface
  when it composes, on its own clock, and nothing made the two take turns. A
  blit landing while DWM was reading produced a composed frame that was part
  new selection and part old, split along one scanline. It appeared next to
  the pointer because the pointer is where the only changing pixels are.

  A frame pulled out of a phone recording of the drag shows it outright: in a
  single frame the right-hand border sits at one x above the split and eight
  pixels to the left of it below.

  The overlay now calls `DwmFlush` after each paint, which blocks until the
  compositor has finished its next frame. Every blit therefore begins just
  after a composition ended, with most of a frame interval to finish in, and a
  blit of the dirty rectangle takes a small fraction of that.

  It also makes the drag cheaper rather than slower: painting is capped at the
  monitor's refresh rate instead of the mouse's report rate, so frames that
  were being composed only to be overwritten before anyone saw them are no
  longer drawn at all. Mouse-moves coalesce while it waits, so the selection
  still tracks the pointer exactly.

## [1.7.5] — 2026-09-28

### Fixed

- **"Run at Startup: On" opened the menu at every login.** The menu appeared
  on its own after a restart, without anything being clicked.

  Two unrelated decisions had been welded into one `if`/`else`: *should setup
  be deferred* (only when a delay is set **and** Windows started the app) and
  *should the menu be shown* (only when the **user** started the app). With a
  delay set, the first branch ran and the menu correctly stayed away. With no
  delay — which is what `On` means — the condition was false, control fell to
  the `else`, and the menu opened along with the tray icon.

  The two questions are now asked separately. A login launch never shows the
  menu, at any delay, including none.

- **Run at Startup silently stopped working after an upgrade.** The executable
  carries its version in its file name, so the registry entry written by
  1.7.4 named `SnipTextProUltra_1.7.4.exe` — a file that upgrading to 1.7.5
  removes. Windows then had nothing to launch, while the menu still read
  `Run at Startup: On`. The entry is now checked against the running
  executable at launch and rewritten when it has gone stale, so an upgrade
  repairs it by itself. Toggling it off and on again is no longer needed.

### Changed

- **A login launch is now stated rather than guessed.** The startup entry
  carries a `--startup` argument, so the program can read what kind of launch
  it is from its own command line. Previously it inferred this from system
  uptime — any launch within two minutes of a boot was assumed to be
  Windows's, which meant that starting the app by hand shortly after a restart
  would swallow the menu. The uptime rule remains as a fallback for entries
  written by earlier versions, and those are rewritten on first launch.

## [1.7.4] — 2026-09-28

### Fixed

- **Text in the editor appeared to vibrate while drawing.** Present since the
  editor was written, and it took a frame-by-frame look at a phone recording to
  pin down: the glyphs were not moving, they were being *resampled
  differently*.

  The canvas re-scaled the full-resolution capture on every `WM_MOUSEMOVE`, and
  because the good resampler is too slow to do that, it switched to a crude one
  for the duration of a drag. `HALFTONE` averages the source pixels that map to
  each destination pixel; `COLORONCOLOR` simply **drops** rows and columns. On
  text — where a glyph stem is one or two pixels wide — dropping a column
  deletes the stem of an `h` or shifts it a pixel, so the whole line changed
  appearance on mouse-down and changed back on mouse-up. In the recorded frames
  `The` reads as `Ihe`, `Everything` as `Lverything`.

  The capture is now scaled **once** and kept, so the crude mode has no reason
  to exist and the good one is used always. The wobble is gone and drawing is
  *faster*, because the scaling no longer happens per mouse-move.

  Only the on-screen preview was ever affected — export always re-blitted the
  untouched original at full resolution, so no saved or copied image was ever
  wrong.

  The cache is bounded by construction: one canvas-sized bitmap, about 4 MB for
  a typical window; drawing, undo, paste and save never touch it; a resize
  replaces it and releases the old one first, so shrinking the window shrinks
  it too; it is dropped entirely when the capture is shown at 1:1, so a small
  capture costs nothing; and it goes with the window.

## [1.7.3] — 2026-09-28

### Changed

- **The menu-fade wait is gone; captures are instantaneous again.** 1.7.2 slept
  250 ms before a menu-initiated capture to outlast the fade. Two things were
  wrong with it.

  It was gated on the wrong setting. `SPI_GETMENUFADE` is "Fade or slide menus
  into view" — the fade *in*. The effect that causes the problem is "Fade out
  menu items after clicking", which is `SPI_GETSELECTIONFADE`. So switching the
  offending checkbox off did not switch the delay off, and the tool felt slow
  for no benefit at all.

  And 250 ms was not enough anyway — a faint menu still made it into the
  capture.

  Rather than correct the constant and raise the delay, all of it is removed.
  An instantaneous capture matters more than an occasional faint menu on one
  of two paths, and the keyboard shortcuts were never affected in the first
  place. The workaround is documented in the README's troubleshooting section:
  use the shortcuts, or untick that one visual effect.

- **Every release CI published was built without whole-program optimisation.**
  `build.bat` has passed `/GL` and `/LTCG` since the beginning; `CMakeLists.txt`
  never did, and CI builds with CMake. So anyone building locally got the
  faster, smaller binary and every downloaded release did not. Fixed, in
  Release configurations only.

- **The pixel loops are a word at a time instead of a byte at a time.**
  `MakeOpaque` was one single-byte store per pixel at a stride of four, which
  the compiler cannot vectorise — 16.6 million scattered writes on a dual-4K
  grab, measured at 8–25 ms. It runs on every capture, every OCR retry pass
  and every recorded frame. Same change to the OCR inverter. Both rewrites
  were verified bit-identical across 200,000 random pixels plus the edge
  cases.

- **The selection overlay and the editor canvas reuse their paint buffer.**
  Both allocated a bitmap the size of the repainted area on *every*
  `WM_MOUSEMOVE`, and the allocation plus first-touch page faults across that
  area was the largest slice of the per-move cost — worth roughly a fifth to a
  third of it, more on a slow machine. The buffer is released before a
  reallocation so a grow never holds two at once.

- **`FillAlpha` created and destroyed a device context and a 1×1 bitmap on
  every call** — four to six times per paint, in a function whose own comment
  said one per paint would be wasteful.

- **The menu no longer walks three folders on every open.** It asked all three
  for their file counts each time, including at launch, and the default
  folders are Pictures and Videos — which Windows 11 often redirects into
  OneDrive, where a directory walk is a network round trip. Measured at
  100–600 ms. Counts are now cached against the directory's last-write time,
  and counted without building a path per file.

  Two things were needed to keep the counts honest. A zero or never-advancing
  timestamp — which FAT, exFAT roots and some network redirectors report — is
  rejected, since keying a cache on a constant would freeze the number for the
  life of the process. And changes this program makes itself invalidate the
  cache explicitly rather than waiting for the filesystem, because a
  directory's timestamp is flushed lazily and waiting would resurrect exactly
  the stale-count bug the live counts were built to prevent.

### Fixed

- **A GDI bitmap leaked** on the path where `CreateDIBSection` returns a handle
  with a null pixel pointer.

## [1.7.2] — 2026-09-28

### Fixed

- **The flyout menu no longer appears in captures.** Four attempts, and this
  one is based on a measurement rather than a theory.

  A diagnostic build reported: fade effect enabled **yes**, menu windows found
  **0**, wait loop **0 iterations, 0 ms**. There is no window. By the time a
  command runs Windows has already destroyed the menu window, and what remains
  on screen is DWM dissolving the surface it last rendered — a ghost with no
  handle. Which explains every earlier failure at once:
  `WDA_EXCLUDEFROMCAPTURE` had nothing to apply to, `SW_HIDE` had nothing to
  hide, waiting for the window to disappear returned instantly because it
  already had, `TPM_NOANIMATION` does not govern the dissolve, and `DwmFlush`
  faithfully returned a frame containing a half-faded menu.

  A wait is the only mechanism left, so the work went into making it cost as
  little as possible and as rarely as possible. It is **250 ms**, and it
  applies only when **both** of these hold:

  - Windows reports the fade effect as on. Switch it off and nothing waits.
  - A **menu** started the capture. `Ctrl+Shift+N` never showed a menu, so the
    shortcut path is exactly as fast as it has always been — which is the path
    anyone using this regularly actually takes.

  A plain sleep, not the message pump the previous attempt used: that pump
  existed on the theory that USER32 drove the fade from a timer on our thread,
  and with no window of ours involved, DWM animates in its own process.

## [1.7.1] — 2026-09-28

### Fixed

- **Another attempt at the menu appearing in captures**, and this time the
  reason the last one failed is known: the suppressor skipped any menu window
  that reported itself invisible. During a fade-out that is precisely what the
  window does — Windows hides it and DWM dissolves the surface it last
  rendered — so the guard added as an optimisation skipped exactly the case
  the function existed for.

  That guard is gone. Beyond it, if and only if Windows reports the fade
  effect as enabled, the capture now waits for the menu window to disappear
  and then settles briefly for the DWM animation. Machines with the effect
  switched off reach none of that and pay nothing.

  `TPM_NOANIMATION` has been passed since 1.6.3 and is not the fix on its own.

## [1.7.0] — 2026-09-28

### Fixed

- **Recordings played back faster than real time on any machine that could
  not sustain the frame rate, and the audio drifted ahead of the picture.**

  The capture loop paced itself against the clock but stamped every frame with
  a fixed interval. When one iteration took longer than that interval — a 4K
  blt, a downscale and an encode on a busy machine — the pacing wait was
  skipped and the loop ran flat out, producing fewer frames than the rate
  claimed while the file still declared `frameCount x frameInterval`. A
  60-second take could come out as a 22-second file. Audio, stamped from real
  sample counts, ran progressively ahead.

  Timestamps now come from `QueryPerformanceCounter`, and each sample's
  duration is the measured gap rather than the nominal one. A dropped frame
  becomes a longer displayed frame instead of a shorter recording.

- **Audio slid ahead of the picture after any quiet stretch.** WASAPI flags a
  packet as silent when the microphone has nothing but silence to hand over,
  which is normal for an idle mic. Those packets consume real time, but the
  audio clock only advanced for packets that were written — so staying quiet
  for twenty seconds put everything said afterwards twenty seconds early, and
  the error accumulated across the take.

- **The Audio menu could select the wrong microphone.** The menu row encoded a
  position in the device list, and `TrackPopupMenuEx` pumps the message queue —
  so plugging in a headset while the submenu was open invalidated the cache and
  the position then meant a different device. The rows now carry the endpoint
  IDs they were drawn from.

- **Two paint handlers could spin a window at 100% CPU forever.** On the rare
  path where `BeginPaint` fails they returned without `EndPaint`, which never
  validates the update region, so Windows resends `WM_PAINT` immediately and
  repeatedly. On the region overlay — topmost, full-desktop, running its own
  modal loop — that is a screen-covering window that Esc cannot reach.

- **Save as PNG could report success on a truncated file.** `WriteFile`'s
  result was discarded, so a full disk or a volume pulled mid-save still
  flashed "Saved".

- **A late-finishing recording could be reported under the wrong name.** If a
  worker was abandoned — a slow `Finalize` on a large file — and the user
  started another recording, the old worker's result could be read as the new
  one's. The result is now tagged with the recording it belongs to, and an
  older worker cannot overwrite a newer result.

- **Two failed-recording paths left a zero-byte `.mp4` behind**, which then
  counted towards *Show Saved Files* as a recording that would not open.

- **A crash on exit** was possible when a worker was abandoned still holding
  Media Foundation objects: `MFShutdown` ran anyway, and releasing those
  afterwards faults. It is now skipped in that case — the OS reclaims the
  allocation microseconds later at process exit.

- **The failure dialog could be used to start a new capture from inside
  itself.** A modal message box runs its own message loop, and the recorder had
  already cleared its recording flag by then, so a hotkey press could stack a
  full-desktop overlay and a blocking five-second wait on top of the dialog
  explaining why the last recording failed.

- **Three places read pixel bytes without flushing GDI first**, against a rule
  the rest of the codebase follows and documents. One of them was the recorder,
  on every frame at any Quality below the top one.

- **The clipboard PNG is encoded before the memory handle is taken**, so an
  allocation failure during encoding cannot unwind past a raw handle.

### Changed

- **Media Foundation starts with the first recording instead of at launch.**
  This is the single largest saving in the program: `MFStartup` commits roughly
  **2–5 MB** and spins up its own worker threads, and for a taskbar utility
  that mostly sits idle — and that many people will never record with at all —
  that was about **half the idle footprint**, permanently, for nothing.

  The first recording now pays that initialisation, tens of milliseconds before
  its first frame. A machine where Media Foundation cannot start says so when
  you try to record rather than at launch, which is strictly better: the launch
  call's result was discarded and nobody was ever told.

- **The frozen desktop is released before the recorder allocates.** Recording a
  region keeps the overlay alive while the recorder starts, deliberately, and
  the overlay holds a snapshot of the whole virtual desktop at 32 bits per
  pixel — **33 MB per 4K monitor, 66 MB for two**. It was staying resident
  while the recorder allocated its own frame buffers and the encoder spun up.
  That was the program's high-water mark and it is now gone.

- **Thirteen unreachable functions, fields and accessors deleted**, most of them
  orphaned when the log was removed in 1.6.3. No behaviour change — every one
  was verified to have no caller anywhere in `src/` or `tests/`.

- **Text Layout → Compare the Two on Sample Text now opens a page that
  actually renders.** The sample page is Markdown as well as HTML, and GitHub
  renders Markdown at its own URL — so the menu row opens readable examples in
  the browser instead of the wall of markup GitHub serves for a `.html` file.
  The link was a placeholder in 1.6.3 for exactly that reason, and closing it
  needed no hosting.

- **The editor moves a completed pen stroke instead of copying it.** A long
  scribble carries every sampled point, hundreds of kilobytes, and it was being
  deep-copied once per stroke.

### Looked at and deliberately not changed

- **The undo stack is capped at 50 snapshots, not by size.** A few long pen
  strokes can make each snapshot large enough that the stack reaches tens of
  megabytes. Capping by point count instead would make undo depth vary
  silently with what you had drawn, which is worse than the memory.
- **The encoder's input queue is unbounded** because throttling is disabled on
  purpose. Re-enabling it would block the capture thread and change frame
  pacing. Worth revisiting only if the growth is ever actually observed.
- **The shutter sound buffers are never freed** — 17 KB, and `PlaySound` reads
  them from its own thread past static destruction. Freeing them is a
  correctness regression, not a saving.
- **GDI+ stays initialised at launch.** Unlike Media Foundation it is used by
  every drawing path, so deferring it would mean auditing all of them for a
  fraction of the benefit.

## [1.6.3] — 2026-09-27

### Fixed

- **The menu really is out of the capture now.** 1.6.2 addressed the wrong
  half of this and the symptom survived.

  The actual cause is a Windows visual effect: **Performance Options → Visual
  Effects → "Fade out menu items after clicking"**, which is on by default.
  With it on, the menu *window* outlives the click and fades over roughly
  200 ms. `TrackPopupMenuEx` has returned and the `HMENU` is destroyed, but the
  row that was clicked is still on screen.

  That is why waiting on `DwmFlush` was not enough. It does exactly what it
  promises and returns a frame that faithfully contains a half-faded menu —
  there was nothing to wait for, because the thing had not begun to disappear.

  The capture path now takes any popup menu window of its own out of the
  capture with `WDA_EXCLUDEFROMCAPTURE`, so there is nothing to wait for
  either. On a build too old for that flag the window is hidden instead. Both
  are immediate.

  Deliberately not a delay: a delay would have to be long enough for the
  slowest machine with the fade enabled, and every machine without it would
  pay that on every capture. This costs one window enumeration, which finds
  nothing on the common path. The second `DwmFlush` 1.6.2 added is now only
  issued when a menu actually had to be dealt with, so an ordinary capture is
  a frame faster than it was.

  `TPM_NOANIMATION` is also passed when raising the menu. It is per-call and
  changes no system setting. It may suppress the fade as well, but the flag is
  documented as affecting how the menu is *displayed*, so the fix does not
  depend on it.

### Changed

- **Renamed throughout: `SnipText` is now `SnipTextProUltra`.** The app name,
  the window titles, the registry key, the Run-at-startup entry, the capture
  folders, the saved file names, the window classes, the resource and manifest
  files, the CMake target.

  **This resets your settings and hides your existing captures.** A rename with
  no migration was the deliberate choice — the alternative was carrying
  migration code forever for a one-time move — so on first run after upgrading:

  | | |
  |---|---|
  | Every setting | back to its default, because the registry key moved |
  | Existing captures | still on disk, in the old `SnipText_*` folders |
  | Show Saved Files | reports none, because it looks in the new folders |
  | Run at Startup | off, with an orphan `Run\SnipText` entry to delete |

  To bring the old captures across, rename the three folders in Explorer:
  `Pictures\SnipText_Screenshot_Images` →
  `Pictures\SnipTextProUltra_Screenshot_Images`, and the same for
  `SnipText_ScreenshotToText_Images` and `Videos\SnipText_Videos`. To remove
  the orphan startup entry:
  `reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v SnipText /f`

  Past release notes and released changelog sections keep the old name, because
  that is what shipped under it.

- **The log file is gone.** No `%LOCALAPPDATA%` folder, no trace on disk.
  `Log.h`, `Log.cpp` and 99 call sites removed, along with the `debugMode`
  setting and the startup environment banner.

  Removing it turned a set of quiet failures into silent ones, which was not
  the intent, so the ones that matter now report on screen instead:

  | | |
  |---|---|
  | Auto-save could not write | `Couldn't auto-save the screenshot to disk` |
  | Sanitize could not remove files | `Settings restored, but some files couldn't be removed` — it used to claim success either way |
  | Run at Startup could not change | `Couldn't set SnipTextProUltra to run at startup` — previously a bare beep |
  | Stop button will be in the recording | warned before the take, not discovered after it |
  | GDI+ or the window failed at launch | a message box; the icon used to do nothing at all, inexplicably |

  The trade is real: a bug on a machine nobody can reproduce on now has no
  trail. The module is in the history at `v1.6.2` if it is ever needed back.

## [1.6.2] — 2026-09-27

### Fixed

- **The flyout menu could appear in the capture on a slower machine.** The
  menu is dismissed and its `HMENU` destroyed before the command runs, but
  destroying a menu does not put the pixels back — the desktop has to be
  composited again without it. On a fast machine that happens before the
  capture; on a slower one it did not, and the row that was just clicked ended
  up in the screenshot.

  Worse than cosmetic for *ScreenshotToText*, because the row's own text —
  `ScreenshotToText a Region…   Ctrl+Shift+3` — was then recognised and copied
  along with everything the user actually wanted.

  The capture now waits on `DwmFlush` before reading the screen. Under DWM the
  windows underneath never need to repaint, since their content was never
  destroyed; all that is missing is a new composition without the menu in it,
  and `DwmFlush` blocks until DWM has finished composing. That is the
  compositor saying the frame is done rather than a guess about how long it
  takes — a sleep would have been both too short on the slowest machine it has
  to work on and wasted time on every machine faster than that.

### Changed

- **The word "Stop" is back in the recording pill**, and stays. Removing it
  confused *what* with *how*: the green frame says a recording is running, but
  only the word says this small box is the thing that ends it. A hand cursor
  and a hover border are discoverable by accident, which is not the same as
  being discoverable.

- **The Text Layout submenu describes both options, not one.** It carried a
  single footer explaining only *Rebuild Paragraphs*, so selecting *Keep Every
  Line Separate* left the menu still explaining the option you had just moved
  away from — and the only way to learn what the other one did was to pick it
  and take a capture. Each option now has its description directly beneath it,
  so you never have to choose in order to find out what choosing would do.

- **Text Layout → Compare the Two on Sample Text** opens a page of eight
  deliberately awkward cases: a wrapped paragraph, a three-column table, a
  column of serial numbers, a list whose items wrap, an indented block, a
  symbol string. Each says what to expect from both settings, which makes it a
  regression check as well as an explanation — if a column of serial numbers
  comes back joined, that is a bug rather than a preference.

  The page is `docs/text-layout-test.html`, and it is also linked from the top
  of the README. **The menu's URL is a placeholder**: GitHub serves a `.html`
  file in a repository as source rather than rendering it, so the link will
  not show the page until it is published somewhere that does.

- **Show Saved Files** sits directly above *Sanitize and Restore Default*
  again, below *Screen Recording Settings* — settings together, then the two
  rows that reach outside the app.

- **The version in the menu's title row no longer has a `v`** —
  `SnipTextProUltra · 1.6.1 · markpelayo`.

- **The executable carries its version**: `SnipTextProUltra_1.6.1.exe`. The
  name is read from `VERSION` by CMake, by `build.bat` and by CI rather than
  written in each, so it cannot drift from what the binary reports about
  itself, and a bumped `VERSION` renames the output with nothing else to
  remember. `release.sh` now also checks the resource block's copy of it.

## [1.6.1] — 2026-09-26

### Fixed

- **The mouse pointer strobed on screen for the whole of a recording.** The
  recorder's per-frame `BitBlt` passed `CAPTUREBLT`, which tells GDI to include
  layered windows — and to do that, the system takes the cursor down and puts
  it back around the blt. Once, for a screenshot, that is imperceptible, which
  is why `Capture.cpp` still uses it. Thirty times a second it is a visible
  flicker.

  The give-away was that the recorded frames were always fine: the flicker
  existed only on the real desktop, never in the file, which is what made it
  look like a display driver problem. The cost of dropping the flag is
  layered-window fidelity in recordings, which is a much smaller problem than
  a pointer that flashes throughout them.

- **The Lift button was missing from the editor on small captures.** The
  minimum window size was raised when the seventh tool was added, but only for
  *resizing* — the window CREATION path had its own copy of the old literal, so
  any capture small enough to hit the floor opened one button short. Both now
  come from one derived constant, and the conversion from client size to frame
  size is DPI-aware, which it was not: `AdjustWindowRectEx` reports non-client
  metrics at 96 DPI regardless of the display, so on a scaled monitor the
  client area could still be dragged under the minimum.

### Added

- **The Stop pill is about a third narrower**, now reading `● 00:06` instead
  of `● 00:06   Stop`. Once the green frame appeared for full-screen
  recordings, the frame says a recording is running and the pill no longer has
  to spell out what it is for — and on a full-screen recording it sits over the
  work for the whole take. It is still one click to stop; the hand cursor and
  the red hover border say so.

  Considered and rejected: removing it entirely on full screen. The frame and
  the pill answer different questions — *what* is being recorded versus *how
  long*, plus the only always-visible way to stop. Falling back to the tray
  icon would reintroduce the exact problem the pill was built for, since
  Windows 11 hides the notification area behind a chevron by default.

- **A red dot beside "Stop Recording" in the menu**, so the menu says a
  recording is running at a glance rather than only in words.

  It is a bitmap rather than a "●" in the label, because a menu draws its text
  in the system colour and a dot typed into the string comes out black or
  white with everything else — the one thing a recording dot must not be. It
  goes in `MENUITEMINFO::hbmpItem`, which puts it in the check-mark gutter,
  spaced and aligned the way Windows spaces its own check marks and still
  correct after a theme change. Sized from `SM_CXMENUCHECK`, and drawn with
  premultiplied alpha, which is what menus expect of a 32-bit bitmap — straight
  alpha renders as a dark halo.

- **Full-screen recordings now get the green dashed frame.** A region covering
  the whole monitor has no outside to put a border in, so the frame was created,
  positioned off the edge of the desktop, and never seen — leaving the corner
  pill as the only indicator.

  For that case the frame is now drawn just inside the screen edges and hidden
  from the capture with `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`, the
  mechanism Windows provides for exactly this. It needs Windows 10 version
  2004; on anything older the frame stays outside and full screen has no frame,
  as before.

  Two guards, because a border wrongly believed to be hidden would be recorded
  into every video: the test is whether the region covers **all four** monitor
  edges rather than any of them, and the affinity is read back and required to
  match exactly — `WDA_EXCLUDEFROMCAPTURE` is `WDA_MONITOR` plus a bit, so an
  older build could accept the call and black the window out instead.

  The same mechanism keeps the Stop pill out of the video when it has to sit
  inside the recorded area, which until now was an accepted limitation.

### Changed

- **Confirmations last 3 seconds instead of 1.6, and say what happened.**
  These are the only feedback the program gives, there being no notification
  banner anywhere in it, and 1.6 seconds was long enough to notice a message
  but not to read one. Every message was rewritten as a sentence:

  | was | is |
  |---|---|
  | `reset` | `Sanitized and restored to defaults` |
  | `98 chars copied` | `Copied 98 characters to the clipboard` |
  | `copied` | `Copied 98 characters again` |
  | `no text found` | `No text found in that capture` |
  | `copy failed` | `Found the text, but the clipboard refused it` |
  | `saved …mp4` | `Recording saved · …mp4` |
  | `⚠ capture failed` | `⚠ The capture failed` |

  The box now also clamps its width to the screen and ellipsises the end
  rather than the middle, since the longest message carries a generated file
  name.

## [1.6.0] — 2026-09-26

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

- **The menu is about 45 px narrower.** A Win32 popup is sized as *widest
  label + widest accelerator*, and the title row was the widest label at 45
  characters, against 29 for the longest command. So that one row was setting
  the width of every row beneath it. Dropping "by" and the doubled spaces
  around the separators takes it to 38.

  Worth recording for anyone tempted to trim it further: there is a floor at
  29, where `Sanitize and Restore Default…` becomes the widest label and takes
  over. Below that, cutting characters from the title buys nothing.

  Also tried and reverted: moving `markpelayo` after a tab, into the
  accelerator column. It is narrower still, and it looks wrong — the name
  lands in a column of `Ctrl+Shift+N` and reads as though it were one of them.

- **Lift**, a seventh tool in the annotation editor, after *Text*. Drag a
  rectangle over any part of the capture and that region becomes a piece you
  can drag somewhere else in the same image — for when pointing at something
  is weaker than showing it. Plain drag copies; **Shift**-drag also blanks the
  source with a colour sampled from the ring of pixels around it, making it a
  cut.

  While Lift is the selected tool the canvas carries a hint along the bottom —
  *Drag to copy a piece · Shift-drag to cut it out* — because a modifier
  nobody knows about is a feature that does not exist. It shows only for this
  tool, which is the only one with a modifier, and hides during a drag. The
  alternative was an eighth toolbar button, which the editor's minimum width
  cannot take.

  It is an ordinary annotation, not an edit: it is selectable, movable,
  resizable and undoable, and **the capture underneath is never modified**.
  That is also why it costs nothing — the piece references the pixels already
  in memory rather than copying them, so it is one blit per repaint and no
  extra allocation.

- **After a Screenshot**, below *Shutter Sound*, with two states named the
  same way *Text Layout* is:
  - **Open the Editor** (default) — unchanged behaviour.
  - **Copy to Clipboard and Close** — the capture goes straight to the
    clipboard and nothing opens. Shutter, a confirmation with the pixel size,
    and you can paste.

  Auto-Save applies either way: with both on, the shot is written to disk
  *and* put on the clipboard, and still nothing opens. Only the two Screenshot
  commands are affected — *Screenshot to Text* never opened the editor.

  A failed clipboard write says so rather than failing silently. Another
  process can hold the clipboard open, and with the editor skipped there is
  nowhere else the capture survives unless Auto-Save happened to catch it, so
  silence would be indistinguishable from success.

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

[Unreleased]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/compare/v1.9.7...HEAD
[1.9.7]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.7
[1.9.6]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.6
[1.9.5]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.5
[1.9.4]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.4
[1.9.3]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.3
[1.9.2]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.2
[1.9.1]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.1
[1.9.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.9.0
[1.8.6]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.8.6
[1.8.5]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.8.5
[1.8.4]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.8.4
[1.8.3]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.8.3
[1.8.2]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.8.2
[1.8.1]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.8.1
[1.8.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.8.0
[1.7.7]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.7
[1.7.6]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.6
[1.7.5]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.5
[1.7.4]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.4
[1.7.3]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.3
[1.7.2]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.2
[1.7.1]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.1
[1.7.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.7.0
[1.6.3]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.6.3
[1.6.2]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.6.2
[1.6.1]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.6.1
[1.6.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.6.0
[1.5.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.5.0
[1.4.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.4.0
[1.3.1]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.3.1
[1.1.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.1.0
[1.0.0]: https://github.com/markpelayo/Windows-Taskbar-SnipTextProUltra/releases/tag/v1.0.0
