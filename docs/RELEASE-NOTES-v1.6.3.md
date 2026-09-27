# v1.6.3

The app is renamed, the log file is gone, and the menu is finally out of the
captures — this time for the right reason.

## Read this before upgrading

**Your settings will reset and your existing captures will not show up.**
Nothing is deleted; the rename was made without migration on purpose, so the
new version reads none of the old state and leaves it behind untouched.

| | |
|---|---|
| Every setting | back to its default — the registry key moved |
| Existing captures | still on disk, in the old `SnipText_*` folders |
| Show Saved Files | reports none, because it looks in the new folders |
| Run at Startup | off, with an orphan `Run\SnipText` entry left behind |

To clear the old state completely:

```
reg delete "HKCU\Software\markpelayo\SnipText" /f
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v SnipText /f
rmdir /s /q "%LOCALAPPDATA%\SnipText"
```

The second line only matters if Run at Startup was on; otherwise the value is
not there and the command says so harmlessly. The third removes the old log
folder, which nothing recreates now.

To bring your captures across, rename the three folders in Explorer:

```
Pictures\SnipText_Screenshot_Images        → Pictures\SnipTextProUltra_Screenshot_Images
Pictures\SnipText_ScreenshotToText_Images  → Pictures\SnipTextProUltra_ScreenshotToText_Images
Videos\SnipText_Videos                     → Videos\SnipTextProUltra_Videos
```

The executable is `SnipTextProUltra_1.6.3.exe`. Because the name carries the
version, this is a different file from 1.6.2 — unpin the old one and pin this.

## Fixed

**The menu really is out of the capture now.**

1.6.2 addressed the wrong half of this and the symptom survived. The actual
cause is a Windows visual effect: **Performance Options → Visual Effects →
"Fade out menu items after clicking"**, which is on by default. With it on the
menu *window* outlives the click and fades over roughly 200 ms —
`TrackPopupMenuEx` has returned and the `HMENU` is destroyed, but the row that
was clicked is still on screen.

That is why waiting on `DwmFlush` was not enough. It does exactly what it
promises and hands back a frame that faithfully contains a half-faded menu.
There was nothing to wait for, because the thing had not begun to disappear.

The capture path now takes any popup menu window of its own out of the capture
with `WDA_EXCLUDEFROMCAPTURE`, so there is nothing to wait for either. On a
build too old for that flag the window is hidden instead. Both are immediate.

Deliberately not a delay. A delay would have to be long enough for the slowest
machine with the fade enabled, and every machine without it would pay that on
every capture. This costs one window enumeration, which finds nothing on the
common path — and the second `DwmFlush` 1.6.2 added is now issued only when a
menu actually had to be dealt with, so an ordinary capture is a frame *faster*
than 1.6.2.

`TPM_NOANIMATION` is also passed when raising the menu. It is per-call and
changes no system setting. It may suppress the fade as well, but the flag is
documented as affecting how a menu is *displayed*, so the fix does not depend
on it.

## Changed

**Renamed throughout: `SnipText` is now `SnipTextProUltra`** — the window
titles, the registry key, the Run-at-startup entry, the capture folders, the
saved file names, the window classes, the resource and manifest files, the
CMake target. Past release notes keep the old name, because that is what
shipped under it.

**The log file is gone.** No `%LOCALAPPDATA%` folder, no trace on disk.
`Log.h`, `Log.cpp` and 99 call sites removed, along with the `debugMode`
setting and the startup environment banner.

Removing it turned a set of quiet failures into silent ones, which was not the
intent. The ones that matter now report on screen instead:

| | |
|---|---|
| Auto-save could not write the file | `Couldn't auto-save the screenshot to disk` |
| Sanitize could not remove the files | `Settings restored, but some files couldn't be removed` — it used to claim success either way |
| Run at Startup could not be changed | `Couldn't set SnipTextProUltra to run at startup` — previously a bare beep |
| The Stop button will be in the recording | warned before the take rather than discovered after it |
| GDI+ or the window failed at launch | a message box — the icon used to do nothing at all, inexplicably |

The trade is real: a bug on a machine nobody can reproduce on now has no trail
to follow. The module is in the history at `v1.6.2` if it is ever needed back.

**Show Saved Files** sits directly above *Sanitize and Restore Default* again.

## Verification

The text normaliser's tests pass. Everything else here is verified by use.

This release is the largest mechanical change the project has had — 99 removed
call sites, a repository-wide rename, and two renamed files — and none of it
has been through a compiler on the author's machine. Treat a green CI run as
the first real evidence, not a formality.

Worth confirming deliberately:

- **The menu is absent from a `ScreenshotToText a Region` capture** with "Fade
  out menu items after clicking" left ON, which is the whole point of the fix.
- **`build.bat` produces `SnipTextProUltra_1.6.3.exe`** — the version is read
  from `VERSION` by new batch-file code, where quoting is easy to get wrong.
- **The five shutter tones, Lift, and recording** still behave, since the log
  removal touched almost every file in the project.
