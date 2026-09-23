// TextNormalizer.h — raw OCR lines to pasteable text.
//
// OCR gives one entry per visual line. That is wrong for prose, where you
// want paragraphs, and right for code and lists. The decision is made from
// GEOMETRY, not from character counts: a line that stops well short of the
// right margin didn't wrap, so its break is real. That single rule does most
// of the work.
//
// An earlier implementation guessed from line length instead. It merged
// adjacent paragraphs into one and glued trailing labels like "Priority:
// High" onto the end of the preceding prose.

#pragma once

#include "OcrLine.h"

#include <string>
#include <vector>

namespace text {

// keepLineBreaks == true bypasses the whole geometry path and returns exactly
// the lines the engine saw, joined with "\n" — the right answer for code,
// logs, identifiers and table cells.
std::wstring Normalize(const std::vector<OcrLine>& lines, bool keepLineBreaks);

// Exposed for the unit tests in tests/.
std::wstring CollapseWhitespace(const std::wstring& input);
bool         IsListItem(const std::wstring& line);

} // namespace text
