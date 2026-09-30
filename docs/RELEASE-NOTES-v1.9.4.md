# SnipTextProUltra v1.9.4

Two small things, both about the same problem: **the keyboard shortcuts worked
but did not say so.**

---

## Shortcuts now flash their button

Press `Ctrl+Z`, `Ctrl+Y`, `Ctrl+C` or `Ctrl+S` and the matching toolbar button
looks clicked for a moment.

| shortcut | button |
|---|---|
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z`, `Ctrl+Y` | Redo |
| `Ctrl+C` | Copy |
| `Ctrl+S` | Save |

**Undo and redo are the pair that needed this.** `Z` and `Y` are adjacent keys
with opposite effects, and on a drawing with only a few marks the result of
hitting the wrong one can be genuinely hard to spot — you are left staring at
the canvas trying to work out whether something came back or went away. The
button tells you directly which command ran.

Copy and Save had a subtler version of the same problem: they already
confirmed themselves in the title bar, but there was nothing tying that
confirmation to the command you pressed.

### Three details worth knowing

**A disabled button does not flash.** `Ctrl+Z` with an empty undo stack stays
completely silent. That is deliberate: *nothing happened* is the honest
answer, and it is information worth having — a flash there would say a command
ran when none did. The same applies while a label is being typed, where
`Ctrl+Z` backs out of the label rather than touching the undo stack.

This is also why the flash is raised **before** the command runs rather than
after. The button's enabled state still describes whether the command is about
to do anything; a moment later it may not, because the last available undo
disables Undo. Doing it in this order also means the press is on screen before
`Ctrl+S` goes off to a dialog or a slow disk, rather than after it comes back.

Only Undo and Redo are ever disabled, so **Copy and Save always flash** — the
command did run. Whether it *succeeded* is the title bar's job, and a
cancelled save dialog or a failed copy says so there.

**Holding a shortcut down looks right.** Auto-repeat restarts the release
timer rather than stacking a new one, so the button stays down for the whole
run of undos and comes back up once when you let go — which is what the
gesture actually is.

**150ms.** Chosen against human vision rather than taste: much under 100ms can
be missed entirely between eye movements, and much over 250ms starts to read
as a stuck button.

### How it is built

`BM_SETSTATE`, not a "pretend pressed" flag of our own. The fake press sets
the button's real internal state, so it travels the same `ODS_SELECTED` path
through `WM_DRAWITEM` as a genuine click.

**Sharing the mechanism is not the same as being interchangeable, and
assuming so cost a bug** — caught by audit, before release, on exactly the
case the feature was built for. A real click releases the button *before*
`WM_COMMAND` runs, so the `EnableWindow(FALSE)` that follows the last undo
always landed on a released button. The flash holds the button down *across*
that disable, reaching pressed-and-disabled: the blue "switched on" face,
used nowhere else but for the selected tool and Pin, wearing a greyed-out
glyph. So the last `Ctrl+Z` in a run would light Undo up as though it had just
been turned on.

Fixed at the draw site rather than by shortening the flash, because the face
was what was wrong: a disabled button now ignores both `active` and `pressed`
and simply goes grey — which is the honest signal that the stack emptied.

`BM_SETSTATE` on an owner-drawn button sends `WM_DRAWITEM` back to the editor
**synchronously**, so the release path clears its own record of which button
is lit *before* it sends anything — the same re-entrancy rule that
`WM_LBUTTONUP` learned the hard way with `ReleaseCapture` in 1.9.1. The button
handle is also dropped, and the timer killed, in `WM_DESTROY`: child windows
are destroyed *after* their parent's `WM_DESTROY`, so a timer firing in that
window would be talking to a handle about to go invalid.

---

## The title-bar confirmation lasts 2 seconds

Copying or saving briefly replaces the window title with *Copied* or *Saved*.
That was 1.2 seconds, which is the worst of both outcomes: long enough to
notice something changed, too short to actually read the word. Now 2 seconds.

If you crop while a confirmation is showing, the title switches to the new
dimensions immediately. The crop is the newer information, and the title it
reverts to afterwards is the right one either way.

---

## Upgrading

Nothing to do. No settings changed, no shortcuts changed, no behaviour
changed — the same keys run the same commands, and now they show it.
