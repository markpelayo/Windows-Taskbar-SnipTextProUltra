# SnipTextProUltra v1.9.6

**The editor now opens with Rectangle selected.**

It was Arrow, which after the 1.9.5 reorder left a fresh editor with the
*middle* of the toolbar lit — and a crop jumping the selection back there.
Rectangle is the most-used tool, and it is now also the first button and the
`1` key, so three things that were three separate arbitrary facts are one:

> the default tool · the leftmost button · the first digit

Cropping now disarms to the default tool too, rather than specifically to
Arrow. Same answer, one rule, and since cropping is almost always followed by
annotating it also lands somewhere useful.

## Nothing you had chosen changes

If you had already picked a tool, you keep it. The remembered tool is
persisted **by name**, so neither the 1.9.5 reorder nor this change disturbs
it. The new default only applies to a fresh install, or after *Sanitize and
Restore Default*.

## One constant, five callers

The default tool is now `kDefaultTool` in `Annotation.h` rather than five
separate mentions of `Tool::Arrow`. Those five all have to agree:

- `currentTool_`'s initialiser
- the string `CurrentTool()` falls back to when nothing is stored
- what `ToolFromKeyValue` returns for a value it cannot parse — missing,
  misspelt, or written by a newer version that had a tool this one does not
- `editor_settings::IsDefault`'s test, which is what decides whether
  *Sanitize* thinks there is anything to restore
- the tool Crop disarms to

Five mentions is four chances to move the default and miss one, and the miss
would have been quiet: a `Sanitize` that believes the settings are dirty
forever, or a fallback tool that disagrees with the opening tool.

`Annotation::tool`'s own member initialiser is deliberately **not** folded in.
That one answers a different question — what a blank `Annotation` claims to be
before anything fills it in — and it never reaches the screen, because every
real mark sets its tool explicitly. Tangling the two together would have
looked tidy and meant nothing.

## Upgrading

Nothing to do.
