# v1.6.0

A bug fix you could see, a bug fix you could not, one new editing tool, and a
round of menu work.

## Fixed

**The hint bar smeared a trail across the screen while dragging a selection.**

The region overlay repaints the union of the old and new selection, grown by a
fixed 90 px to take in the border, the handles and the size readout. The hint
bar — *Drag to select · Space to pick a window · Esc to cancel* — is 560 px
wide and centred on the selection, so for any selection narrower than 380 px
it stuck out past that region at both ends, and the parts sticking out were
never repainted.

The Record button had the same problem, and both are worse than a fixed
padding can fix: each flips to the other side of the selection when it runs
out of room, moving an arbitrary distance in a single step. Both rectangles
are now asked for before and after the selection changes and invalidated
explicitly.

**Choosing a shutter sound silently reset Text Recognition to Auto.**

The handler for *Built-in Shutter* had picked up a stray
`settings::Remove(kOcrEngine)` from a bad paste. Nothing reported it because
both settings are invisible until you open the menu, and the menu redraws from
the registry, so the check mark moved and looked deliberate. The row that
carried it is gone, and the line with it.

## Added

### Lift — a seventh tool in the annotation editor

Every other tool draws on top of the picture. **Lift** moves the picture
itself: drag a rectangle over any part of the capture and that region becomes
a piece you can drag somewhere else in the same image.

It is for when pointing at something is weaker than showing it. Instead of an
arrow saying *this belongs over there*, put it over there.

| | |
|---|---|
| **drag** | Copy. The original stays; pull the piece aside and it is still there. |
| **Shift**+drag | Cut. The source is blanked with a colour sampled from the ring of pixels around it. |

Either way the piece lands exactly on top of where it came from, so nothing
appears to happen until you drag it. What you do next is what makes it a copy
or a move. While Lift is selected the canvas carries a hint saying so, because
a modifier nobody knows about is a feature that does not exist.

It is an ordinary annotation, not a pixel edit: selectable, movable, resizable
and undoable, and **the capture underneath is never modified** — so even the
Shift version is fully reversible. It also costs nothing, because the piece
references the pixels already in memory rather than copying them: one blit per
repaint, no allocation.

### After a Screenshot

A new submenu below *Shutter Sound*, with both states named:

- **Open the Editor** (default) — unchanged behaviour.
- **Copy to Clipboard and Close** — the capture goes straight to the clipboard
  and nothing opens. Shutter, a confirmation with the pixel size, and you can
  paste.

Auto-Save applies either way: with both on, the shot is written to disk *and*
put on the clipboard, and still nothing opens. Only the two Screenshot
commands are affected — *Screenshot to Text* never opened the editor.

A failed clipboard write reports itself rather than failing silently. Another
process can hold the clipboard open, and with the editor skipped there is
nowhere else the capture survives unless Auto-Save happened to catch it.

### Five shutter sounds

Classic (the existing sound, unchanged and still the default), SLR Camera,
Aperture, Soft Click and Snap. Choosing one plays it, so the list auditions
itself. All five come from one parameterised synthesiser, and each is built on
first use — a tone you never pick is never generated.

On *Aperture*: Apple's screenshot sound is their audio asset and is not
something to copy into this binary. Aperture is an original synthesis with a
similar character — bright and tight rather than a heavy mechanical thunk. A
family resemblance, not a reproduction.

### Recording file size

**File Size** under Screen Recording Settings: Smaller (new default), Balanced
(the previous behaviour) and Detailed, plus an opt-in **Use H.265 When
Available**. Screen content barely changes between frames and compresses far
better than camera footage, so the old fixed bitrate was spending most of its
budget encoding a static desktop very precisely.

H.265 is off by default deliberately: it halves the size again but needs a
modern player, and a recording that will not open on the machine you sent it
to is not a smaller file. If the machine has no HEVC encoder the recorder
falls back to H.264 on its own.

### Dev builds identify themselves

A build from a working tree reads `v1.6.0 (40a7c7c)` in the menu and the log,
with a trailing `+` when the tree had uncommitted changes. A tagged release
reads `v1.6.0`. During a round of UI changes the version number is identical
on every build and so cannot answer "is this the thing I just changed?".

## Changed

Menu labels, so every row says which of the three things it belongs to rather
than relying on position to imply it:

- *Shortcuts* → **Change Keyboard Shortcut**
- *Record Region…* → **Screen Record a Region…**
- *Record Full Screen* → **Screen Record Full Screen**
- *Video Settings* → **Screen Recording Settings**

*Show Saved Files* and *Screen Recording Settings* swapped places. The title
row is shorter — `SnipTextProUltra · v1.6.0 · markpelayo` — which takes about
45 px off the whole menu, because a Win32 popup is sized as *widest label +
widest accelerator* and that row was the widest label.

**Join Wrapped Lines is now a Text Layout submenu** with both states named:
*Rebuild Paragraphs* (default) and *Keep Every Line Separate*.

This is the third name the setting has had, and the previous two failed the
same way. *Keep Line Breaks* described the state you were switching away from.
*Join Wrapped Lines* described only half of what the other state does: the
geometry pass rejoins wrapped lines **and** inserts a blank line where the
original had a bigger gap. On a capture with nothing wrapped in it — a chat
list, a table, anything already truncated with an ellipsis — the joining half
does nothing, so the only visible effect was blank lines the name never
mentioned. A checkbox can only name one of its two states. The registry key is
unchanged, so nobody's setting resets on upgrade.

**CI runs the fast build on every push and the slow one only on tags.** Every
commit used to wait about twenty minutes for a full static Tesseract build via
vcpkg before anything was checked at all.

## Not done — and why

**MKV and AVI recording.** Neither is possible here, and neither would help.
Media Foundation picks a media sink from the file extension, and the sinks
Windows ships are MPEG-4 and ASF. There is an MKV *source* but no MKV sink,
and no AVI sink in either direction; writing either means embedding a
third-party muxer, and a static FFmpeg is tens of megabytes.

More to the point, a container does not compress anything — it is a wrapper
around streams that are already encoded. Remuxing the same H.264 from MP4 to
MKV changes the file by a few kilobytes over an entire recording. MKV files
are often smaller because they were *encoded* differently. The codec and the
bitrate set the size, which is what the new File Size menu exposes.

**A narrower menu than this.** The gap between the label column and the
shortcut column is computed and painted by Windows; no flag or metric shrinks
it. Matching a hand-drawn mockup exactly would mean owner-drawing the menu and
hand-painting the text, check marks, submenu arrows, hover highlight,
separators, disabled states, theming and per-monitor DPI — several hundred
lines, permanently, for a cosmetic gain, with theming and DPI the first things
to break. The title row was shortened instead, which is the part that could be
fixed without giving up a native menu.

## Verification

The text normaliser's tests pass. Everything else in this release is verified
by use, not by test: the overlay repaint fix, Lift, the clipboard path, the
five tones and the recording changes have no automated coverage.

SHA-256 of the executable is published beside it. It is not a signature — it
only lets you confirm the file you downloaded is the file this run produced.
