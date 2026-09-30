# The tray menu

Everything the program does, apart from the editor, is reached from one menu
on the taskbar icon. Either mouse button opens it — a left-click that did
something different from a right-click would be a trap.

This is the whole layout, top to bottom, with what each row is for. Rows whose
text changes with state are shown with the state in `‹angle brackets›`.

```
SnipTextProUltra 1.9.7 · Mark Pelayo          ← click opens the repository
────────────────────────────────────────
Screenshot a Region…              ‹shortcut›
Screenshot Full Screen            ‹shortcut›
────────────────────────────────────────
ScreenshotToText a Region…        ‹shortcut›
ScreenshotToText Full Screen      ‹shortcut›
Copy Last Text Again  /  No text captured yet
────────────────────────────────────────
Screen Record a Region…           ‹shortcut›     ┐ replaced by
Screen Record Full Screen         ‹shortcut›     ┘ "Stop Recording (MM:SS)"
────────────────────────────────────────           while recording
Change Keyboard Shortcut                     ▸
    Screenshot a Region                 Ctrl+Shift+1
    Screenshot Full Screen              Ctrl+Shift+2
    ScreenshotToText a Region           Ctrl+Shift+3
    ScreenshotToText Full Screen        Ctrl+Shift+4
    Screen Record a Region              Ctrl+Shift+5
    Screen Record Full Screen           Ctrl+Shift+6
    ────────────────────────────────────
    Only inside the editor:                       ← section header, not a row
    Close the Screenshot Editor                Esc
    Rectangle                                    1
    Ellipse                                      2
    Arrow                                        3
    Line                                         4
    Pen                                          5
    Text                                         6
    Lift                                         7
    Crop                                         8
    Keep the Editor on Top                       9
    ────────────────────────────────────
    Reset to Defaults
Text Layout                                   ▸
    ● Rebuild Paragraphs
          Rejoins sentences that wrapped, and puts
          a blank line between blocks.
    ────────────────────────────────────
    ○ Keep Every Line Separate
          One line of text for every line on screen.
          Nothing joined, nothing inserted.
    ────────────────────────────────────
    Compare the Two on Sample Text…
Text Recognition                              ▸   ← only in the full build
    ● Auto — Windows, then the fallback
    ○ Windows only — fastest
    ○ Fallback only — reads symbols and codes
Shutter Sound                                 ▸   ‹checked when on›
    ○ Off
    ────────────────────────────────────
    ● ‹five built-in tones›
    ────────────────────────────────────
    Custom Sound…  /  Custom: ‹file name›
    ────────────────────────────────────
    Preview
After a Screenshot                            ▸
    ● Open the Editor
    ○ Copy to Clipboard and Close
    ────────────────────────────────────
    Auto-Save still applies either way.
☐ Auto-Save Images
☐ Keep the Editor on Top
☑ Show Tool Hints in the Editor
Save Locations                                ▸
    Screenshots                               ▸   ‹: FolderName if moved›
        ‹the current path›                        ← greyed, for reading only
        ────────────────────────────────
        Choose Folder…
        Reset to Default
    ScreenshotToText Images                   ▸
    Videos                                    ▸
Screen Recording Settings                     ▸
    Frame Rate: ‹n› fps                       ▸
    Quality: ‹short name›                      ▸
    File Size: ‹short name›                    ▸
        ● ‹compression choices›
        ────────────────────────────────
        ☐ Use H.265
              H.265 halves the size again,
              but older players can't open it.
    ────────────────────────────────────
    ☐ Capture Mouse Cursor
    ☐ Capture Mouse Clicks
    ────────────────────────────────────
    Audio: ‹Do Not Record | device name›       ▸
        ● Do Not Record Audio
        Microphone:                               ← section header
        ○ ‹each microphone found›
Show Saved Files                              ▸   ‹disabled if all empty›
    Screenshots (‹n›)
    ScreenshotToText Images (‹n›)
    Videos (‹n›)
Sanitize and Restore Default…                     ‹disabled if nothing to do›
────────────────────────────────────────
Run at Startup: ‹Off | On | n s›              ▸   ‹checked when enabled›
    ○ Off
    ○ On
    Delay for:                                    ← section header
    ○ 5 s   10 s   15 s   20 s   30 s   60 s
────────────────────────────────────────
Quit SnipTextProUltra
```

`▸` is a submenu. `☐`/`☑` are checkboxes — independent toggles. `●`/`○` are
radio-style choices where exactly one applies.

---

## What each row is for

### The title row

The app name and author. Clicking it opens the repository. It says the app
name rather than the repository name, because the repo's
`Windows-Taskbar-` prefix names the platform the program is already running
on — and it made this row the widest label in the menu, which sets the width
of every row beneath it.

### The three capture pairs

**Screenshot** takes a picture. **ScreenshotToText** takes a picture and reads
the text out of it. **Screen Record** records video. Each comes in a *Region*
and a *Full Screen* form, and each carries its current shortcut in the
right-hand column — read from the live binding, so rebinding one updates this
immediately.

*Copy Last Text Again* re-copies the most recent OCR result, so a clipboard
you overwrote is recoverable without taking the shot again. It reads *No text
captured yet* and is disabled until there is something to copy.

While a recording is running, both record rows are replaced by a single **Stop
Recording (MM:SS)** row with the elapsed time.

### Change Keyboard Shortcut

All sixteen bindings and their current keys. Pick one, press the combination,
Enter to save. `Delete` unbinds; `Esc` cancels.

The split matters more than it looks. The **six above the line are global** —
claimed from the whole system with `RegisterHotKey`, so they are taken away
from every other program, which is why an unmodified key is refused for them
unless it is a function key. The **ten below** are matched inside the editor
and register nothing, which is what makes a bare `Esc` and the bare digits
`1`–`9` safe defaults there.

### Text Layout

What OCR does with line breaks. **Rebuild Paragraphs** rejoins lines that
wrapped and separates blocks with a blank line — right for prose. **Keep Every
Line Separate** preserves the screen's line structure exactly — right for a
table, a list, or a chat log.

Two named rows rather than one checkbox, because a checkbox can only name one
state and the other is always inferred. This setting had two previous names
that each described only half of what it did. *Compare the Two on Sample
Text…* opens a page showing the same text through both, so you can choose
without having to pick one and find out.

### Text Recognition

Which OCR engine to use. **Only appears in the full build** — the
dependency-free build has one engine, and a row offering one option is noise.

*Auto* tries the Windows engine and falls back. *Windows only* is fastest.
*Fallback only* is slower but reads symbols, codes and unusual characters the
Windows engine drops.

### Shutter Sound

Whether a capture makes a noise, and which. *Preview* plays the current choice
without taking a screenshot, so you can audition before committing. Choosing a
built-in tone also un-mutes and clears a custom sound, so picking from the list
never appears to do nothing.

### After a Screenshot

Whether a capture opens the editor or goes straight to the clipboard and
closes. The footer states what the rows cannot: **Auto-Save applies either
way**, so choosing the fast path does not mean losing the file.

### The three toggles

**Auto-Save Images** writes every capture to disk the moment it is taken —
before you annotate anything, which is what makes redaction safe to undo.

**Keep the Editor on Top** makes editor windows stay above other windows, so
you can work beside the thing you captured. The editor has its own button for
this, and the two always agree.

**Show Tool Hints in the Editor** controls the one-line modifier hint along the
bottom of the canvas. On by default; switch it off once you know the
modifiers. The information stays available in this menu and in the tool
tooltips.

### Save Locations

Where each of the three kinds of file goes. Each submenu shows the current
path as a greyed, unclickable row — it is there to read, not to click — then
*Choose Folder…* and *Reset to Default*. The parent row grows a
`: FolderName` suffix only once the folder has been moved, so the name appears
only when it carries information.

### Screen Recording Settings

Frame rate, quality, file size, whether the cursor and clicks are drawn in,
and which microphone. Every parent row states its current value, so the
settings are readable without opening five submenus.

*Use H.265* halves the file again, with the trade named directly underneath:
older players cannot open it.

### Show Saved Files

Opens each folder in Explorer, with its file count in the row. The whole
submenu is disabled when all three are empty, so it tells you there is nothing
there rather than opening to three dead rows.

### Sanitize and Restore Default…

Sends every saved capture to the Recycle Bin and returns every setting to its
default. The confirmation dialog itemises exactly what will go, with live
counts, and its destructive button says what it does instead of saying *OK* —
with *Cancel* as the default, because Enter should never be the key that
throws away a folder of screenshots.

Disabled when there is genuinely nothing to do: no files and no setting
changed from its default.

### Run at Startup

Whether the program launches with Windows, and after how long. The delay
exists because a utility that starts at the same moment as everything else
competes with them; a few seconds later is invisible to you and much kinder to
a slow boot.

The parent row states the current setting and is checked whenever startup is
enabled at any delay, so the state is readable without opening the submenu.

---

## Two things about how the menu is built

**It is rebuilt from scratch every time it opens.** The original version
refreshed at the end of whichever action changed something, and that failed in
a specific way: a screenshot wrote a file without refreshing, so the saved
count only moved when some later, unrelated action happened to rebuild the
menu. Rebuilding on display makes every count correct by construction.

**Section headers are disabled menu items.** Windows has no header item type,
so *Only inside the editor:*, *Microphone:* and *Delay for:* are rows with no
command. They use `MFS_DISABLED` rather than `MFS_GRAYED`, which makes them
unselectable without the heavier greyed-out styling that reads as *broken*.
