# Text Layout test page

Capture each numbered block below with **ScreenshotToText a Region**
(`Ctrl+Shift+3`), paste the result somewhere, then switch
**Settings → Text Layout** to the other option and capture the same block
again.

Capture only the block itself. The *Expected* line under each one is a note,
not test material.

**Text Layout** has two settings and neither is right for everything, so this
page shows what each does to the same text rather than describing it. Each case
says what to expect from **both**, which makes it a bug check as well as an
explanation — if a column of serial numbers comes back joined into one line,
that is a defect and worth reporting.

---

## 1. A paragraph that wraps

The compressor on line three has been tripping its thermal overload roughly every forty minutes since the belt was replaced on Tuesday. The technician who did the work noted that the tensioner was already at the end of its travel, so the belt may be slightly short rather than incorrectly fitted.

*Expected* — **Rebuild Paragraphs**: one continuous paragraph, as written.
**Keep Every Line Separate**: four or five separate lines, broken wherever the
page happened to wrap them.

---

## 2. Two blocks with space between them

Asset 4471 was taken out of service at 09:20 and tagged for inspection.

<br><br>

The replacement unit arrived the same afternoon but could not be commissioned because the isolator had not been certified.

*Expected* — **Rebuild Paragraphs**: two paragraphs with a blank line between
them. **Keep Every Line Separate**: the same lines with no blank line — the gap
is not represented at all.

---

## 3. A table — the case worth deciding on

| Work order | Task | Status |
|---|---|---|
| WO-10482 | Pump seal replacement | Overdue |
| WO-10483 | Quarterly belt inspection | Scheduled |
| WO-10484 | Bearing temperature check | Complete |

*Expected* — this is the one that decides it. Check whether the three columns
come back as **three separate values** or get **joined into one line**. If they
are joined you cannot tell afterwards where one column ended and the next
began: the structure is gone, not just reformatted. That is the unrecoverable
failure, and the reason *Keep Every Line Separate* is the safer single choice
if you ever want to drop the setting.

---

## 4. Short lines that did not wrap

```
WO-10482
WO-10483
WO-10484
SN-88-4471-C
SN-88-4471-D
```

*Expected* — **both**: five separate lines. No line reaches the right margin,
so Rebuild should not join them. If it does, that is a bug.

---

## 5. A list whose items wrap

1. Isolate the unit at the local disconnect and apply your own lock before opening any panel.
2. Verify zero energy at the terminals with a meter you have just proved on a known live source.
3. Record the reading in the work order before starting.

*Expected* — **Rebuild Paragraphs**: three numbered items, each rejoined into
one line. It should *not* run item 1 into item 2 — the numbering is what stops
it. **Keep Every Line Separate**: five or six lines, with items broken
mid-sentence.

---

## 6. Mixed: heading, prose, then data

**Maintenance note**

The gearbox was topped up rather than drained because the oil sample came back clean and the next scheduled change is only three weeks out.

```
Oil: ISO VG 220
Added: 0.4 L
Sample: PASS
```

*Expected* — the realistic case: one capture with prose that wants rejoining
next to data that does not. Whichever setting you choose, one half of this
comes back wrong. Worth seeing which half you mind less — that is the honest
cost of removing the setting.

---

## 7. Symbols and mixed characters

<h3>Magic Word</h3>

<h3>@#4!TW$RH^%&amp;CFG?:</h3>

*Expected* — not about Text Layout. This is the case that needed the second OCR
engine: both lines should come back exactly as shown, with no character changed
into one that makes a word. **Rebuild** will put a blank line between them,
because the gap is large.

---

## 8. An indented continuation

The pump was rebuilt in March and has run without complaint since, which is why the vibration reading came as a surprise.

> Indented follow-up: the reading was taken with the guard removed, which may account for some of it.

*Expected* — **Rebuild Paragraphs**: the indented block stays separate rather
than being run onto the end of the first — the indent is what stops it. If it
gets joined, that is a bug.

---

## Notes

**Case 3** is the one to decide on. **Case 6** is the one showing there is no
setting that gets everything right, which is why this is a menu and not a
constant.

This page is Markdown so that GitHub renders it as a page — nothing to
download, nothing to host. There is also
[an HTML version](text-layout-test.html) with tighter control over widths and
gaps, for when a case needs exact geometry; open it from a local clone.
