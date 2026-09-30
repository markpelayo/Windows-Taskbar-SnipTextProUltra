# SnipTextProUltra v1.9.5

Number keys for every tool, a reordered toolbar, a blue default, and the hint
line you asked about is now switchable.

---

## The toolbar is reordered

| | |
|---|---|
| before | Arrow · Rectangle · Ellipse · Line · Pen · Text · Lift · Crop |
| **now** | **Rectangle · Ellipse · Arrow** · Line · Pen · Text · Lift · Crop |

The two shapes people reach for most are now first, next to the slider that
sizes them. Everything from Line onwards is unchanged.

This was done by reordering the `Tool` enum rather than by adding a separate
display-order array, which makes **enum order, toolbar order and number-key
order one list instead of three**. It is safe because nothing stores a tool as
a *number*: the remembered tool is persisted as a string, the button ids are
rebuilt on every launch, and every `switch` over the enum is by name. That is
now written down next to the enum, because it is exactly the kind of
invariant that stops being true quietly.

---

## Number keys

| key | does |
|---|---|
| `1` – `8` | select the eight tools, left to right |
| `9` | toggle Keep the Editor on Top |

All nine are rebindable: **Change Keyboard Shortcut → Only inside the
editor**, below Esc. Hovering a tool button now shows its key too, read from
the live binding — so if you rebind one, the tooltip says the new key rather
than confidently naming the old one.

### Why bare digits are safe here

A bare unmodified key is normally a terrible shortcut, and the program
actively refuses to let you bind one to the six *global* commands. These are
different because they are **editor-local**: they are never handed to
`RegisterHotKey`, so they take nothing away from any other program on the
machine, and they only do anything while an editor window is in front.

The other half is that the editor's key handler **stands down completely
while a text label is being typed**. Typing "3 items" into a label does not
switch to the third tool. That guard already existed — it is what makes a
bare Esc safe as the close shortcut — and the number keys are checked after
it, not before.

Both the tool keys and `9` work by **sending the same `WM_COMMAND` that
clicking the button sends**, rather than repeating what the button does.
Selecting a tool has to commit an open label and hand focus back to the
canvas; Pin has to write the setting exactly the way the tray row writes it
and then tell every other open editor. Two copies of either would drift apart.

---

## The default colour is blue

`#007AFF`, was `#34C759` green.

It is already one of the nine swatch presets, so the grid shows the default as
*selected* rather than as a tenth colour nobody picked.

Deliberately **not** the system accent blue that the selection rectangle and
handles are drawn in. They look alike, which is the point — but a mark drawn
in exactly the selection colour would be hard to tell apart from its own
dashed outline and grips the moment you clicked it.

While changing it, four copies of that colour across three headers collapsed
into one definition, in `Annotation.h`, where the default colour of an
annotation belongs.

---

## Hints can be switched off

**Show Tool Hints in the Editor**, in the tray menu, on by default. It
controls the one-line hint along the bottom of the canvas — *Drag for an
outline · Shift for a square · Ctrl to fill*.

You were right that it is clutter once you know it. The question was where
the switch belongs, and the answer is the menu rather than the toolbar:

- **Not a toolbar button.** The bar is already eight tools and five commands,
  and every one of them is something you *do*. A preference that changes what
  the window *says* is a different kind of thing, and it belongs where the
  other preferences are. It also costs no width — and that bar's minimum
  width is what sets the editor's minimum window size, so a ninth button
  there is not free.
- **Not only a wiki page.** A page does not reduce clutter, and nobody reads
  documentation in the middle of taking a screenshot.

What makes switching it off safe is that the hint is no longer the only copy
of the information. It is now written down in three places: **Change Keyboard
Shortcut** lists every binding, the **tooltips** name each tool's key, and the
README documents the modifiers. The hint bar is the convenient copy, not the
record.

If you would rather it faded automatically after a few uses instead, say so —
that is a different design and worth doing properly rather than bolting on.

---

## Four bugs caught before release

None was reported, and none would have been easy to attribute. Three of the
four share one root: **nine new actions turned "the one exception" into "ten
of them"**, and three separate places had quietly been written assuming there
would only ever be one. The fourth was a plain arithmetic collision.

### Esc-to-cancel would have destroyed Esc-to-close

The worst of the three. The rebind window lets Esc *cancel* — except for
Close the Screenshot Editor, where Esc has to be bindable or unbinding it once
would make its own default unreachable forever.

That exception was tested with `IsGlobal(action)`, which meant exactly
`CloseEditor` until this release. Widening `IsGlobal` widened the exception to
all ten local actions, and the consequence was not simply "Esc stops
cancelling here":

1. You open *Change Keyboard Shortcut → Rectangle*, change your mind, press
   Esc — the universal cancel, and what the window advertises for the six rows
   just above.
2. Esc is recorded as the candidate binding instead.
3. Enter commits it.
4. The de-confliction pass takes a combination away from whoever else holds
   it — and now finds `CloseEditor`, holding Esc, *in the same scope*.
5. **Esc no longer closes the editor. Permanently, and nothing says so.**

Now tested against `CloseEditor` by name, which is the only action the
exception was ever for. The footer text uses the same condition, because that
one grey line is the only warning the window gives.

### A menu command id collision

`ID_SHORTCUT_BASE` was 1080, with room for the seven shortcut rows that
existed. Going to sixteen rows ran it to 1095 — **straight through
`ID_ENGINE_AUTO`, `ID_ENGINE_WINDOWS` and `ID_ENGINE_TESSERACT` at
1090–1092.**

So three of the new shortcut rows would have silently changed the OCR engine,
and choosing an OCR engine would have opened a rebind dialog. Nothing would
have warned about it at any point: menu command ids are plain `int`s, and a
collision is just two names for one number.

Moved to 1300, clear of every other block, with a `static_assert` so the next
version of this mistake fails at compile time rather than in the menu.

### Nine bare number keys, system-wide

`IsGlobal` was written as `action != CloseEditor` — a list of exceptions.
Appending nine editor-only actions to that list would have registered
**bare `1` through `9` as global hotkeys**, taking those keys away from every
other program on the computer.

It is now a threshold: everything from `CloseEditor` onwards is editor-local.
A threshold cannot be forgotten the next time an editor-only action is added;
a list of exceptions can, and this one would have been.

### An invariant documented as enforced, and enforced nowhere

`kToolActionCount == kToolCount` — eight tools, eight `SelectToolN` actions —
was written down as the seam holding the two enums together and checked by
nothing. Adding a ninth tool would have failed in the quietest way available:
no shortcut for it, the dispatch loop silently never matching it, and
`SelectTool1 + 8` — which is `TogglePin` — printing **Pin's key** in the ninth
tool's tooltip. A wrong but entirely plausible string, which is worse than a
crash. It is now a `static_assert`.

---

## Upgrading

Nothing to do. Your remembered tool, colour and stroke width carry over —
the tool is stored by name, so the reorder does not disturb it.

Two things to notice: **new marks are blue** unless you had already picked a
colour (a stored choice is untouched), and **the tools have moved**, with
Rectangle now first and therefore `1`.
