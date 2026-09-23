#include "TextNormalizer.h"

#include "Util.h"

#include <algorithm>
#include <cmath>

namespace text {
namespace {

// The eleven characters treated as bullet markers.
bool IsBullet(char32_t c) {
    switch (c) {
    case 0x2022:  // BULLET
    case 0x25E6:  // WHITE BULLET
    case 0x2023:  // TRIANGULAR BULLET
    case 0x00B7:  // MIDDLE DOT
    case 0x2027:  // HYPHENATION POINT
    case 0x25AA:  // BLACK SMALL SQUARE
    case 0x2013:  // EN DASH
    case 0x2014:  // EM DASH
    case 0x002D:  // HYPHEN-MINUS
    case 0x002A:  // ASTERISK
    case 0x002B:  // PLUS SIGN
        return true;
    default:
        return false;
    }
}

} // namespace

std::wstring CollapseWhitespace(const std::wstring& input) {
    const std::vector<char32_t> points = util::ToCodePoints(input);

    std::vector<char32_t> out;
    out.reserve(points.size());
    bool pendingSeparator = false;

    for (char32_t c : points) {
        if (c == 0x200B) continue;   // zero-width space, deleted outright
        if (util::IsUnicodeWhitespace(c)) {
            // Splitting on runs of whitespace and rejoining with one space
            // trims both ends and collapses the interior in a single pass.
            if (!out.empty()) pendingSeparator = true;
            continue;
        }
        if (pendingSeparator) {
            out.push_back(U' ');
            pendingSeparator = false;
        }
        out.push_back(c);
    }
    return util::FromCodePoints(out);
}

bool IsListItem(const std::wstring& line) {
    const std::vector<char32_t> points = util::ToCodePoints(line);
    if (points.empty()) return false;

    if (IsBullet(points[0])) {
        // "• item" or "- item". A bare dash with no following space is a
        // hyphen, not a bullet.
        return points.size() >= 2 && util::IsUnicodeWhitespace(points[1]);
    }

    size_t digits = 0;
    while (digits < points.size() && util::IsUnicodeDigit(points[digits])) ++digits;

    // Capped at 3 digits so a year ("2026.") or a decimal ("123.45 total")
    // isn't mistaken for a list marker. The `digits < size` test also makes
    // the index below safe.
    if (digits >= 1 && digits <= 3 && digits < points.size()) {
        const char32_t marker = points[digits];
        if (marker == U'.' || marker == U')') {
            if (digits + 1 == points.size()) return true;   // "1." alone
            return util::IsUnicodeWhitespace(points[digits + 1]);
        }
    }
    return false;
}

std::wstring Normalize(const std::vector<OcrLine>& rawLines, bool keepLineBreaks) {
    // Stage 1 — clean, and drop lines that cleaned to nothing. Their geometry
    // goes with them, so margins and gaps are measured only between lines
    // that actually carry text.
    std::vector<OcrLine> lines;
    lines.reserve(rawLines.size());
    for (const OcrLine& raw : rawLines) {
        OcrLine line = raw;
        line.text = CollapseWhitespace(raw.text);
        if (!line.text.empty()) lines.push_back(std::move(line));
    }
    if (lines.empty()) return std::wstring();

    // Stage 2 — the code-and-lists shortcut. The entire geometry path below
    // is skipped: no median height, no margins, no gap, no de-hyphenation.
    if (keepLineBreaks) {
        std::wstring out = lines.front().text;
        for (size_t i = 1; i < lines.size(); ++i) {
            out.append(L"\r\n");
            out.append(lines[i].text);
        }
        return out;
    }

    // Stage 3 — document-level statistics.
    std::vector<double> heights;
    heights.reserve(lines.size());
    for (const OcrLine& line : lines) heights.push_back(line.height);
    std::sort(heights.begin(), heights.end());
    // Integer division, so this is the upper median for an even count — not
    // the average of the two middle values.
    const double medianHeight = heights[heights.size() / 2];

    double rightMargin = lines.front().maxX;
    double leftMargin  = lines.front().minX;
    for (const OcrLine& line : lines) {
        rightMargin = (std::max)(rightMargin, line.maxX);
        leftMargin  = (std::min)(leftMargin, line.minX);
    }
    // Six per cent of the text block's width, floored so the tolerance can
    // never be zero. Scaling with the block means a narrow snip and a
    // full-screen capture behave the same.
    const double xTolerance = (std::max)(rightMargin - leftMargin, 0.0001) * 0.06;

    // Stage 4 — join or break, one decision per adjacent pair.
    std::wstring output = lines.front().text;
    for (size_t index = 1; index < lines.size(); ++index) {
        const OcrLine& previous = lines[index - 1];
        const OcrLine& line     = lines[index];

        // previousBottomEdge - currentTopEdge. Negative when the boxes
        // vertically overlap, which is meaningful: a negative gap correctly
        // fails both thresholds below. No abs, no clamp.
        const double gap = (previous.midY - previous.height / 2.0)
                         - (line.midY + line.height / 2.0);

        const bool biggerThanNormalLeading = gap > medianHeight * 0.8;
        const bool previousReachedMargin   = previous.maxX >= rightMargin - xTolerance;
        const bool isIndented              = line.minX > leftMargin + xTolerance;
        const bool startsNewItem           = IsListItem(line.text);

        // A continuation of a wrapped line only when all four hold.
        const bool wraps = !biggerThanNormalLeading
                        && previousReachedMargin
                        && !isIndented
                        && !startsNewItem;

        if (wraps) {
            const std::vector<char32_t> incoming = util::ToCodePoints(line.text);
            if (!output.empty() && output.back() == L'-' &&
                !incoming.empty() && util::IsUnicodeLowercase(incoming.front())) {
                // A word split across lines: "manage-" + "ment".
                output.pop_back();
                output.append(line.text);
            } else {
                output.append(L" ");
                output.append(line.text);
            }
        } else {
            // The 0.8 test decided *whether* to break; this one decides how
            // hard. Both are relative to the same median height.
            output.append(gap > medianHeight * 1.6 ? L"\r\n\r\n" : L"\r\n");
            output.append(line.text);
        }
    }
    return output;
}

} // namespace text
