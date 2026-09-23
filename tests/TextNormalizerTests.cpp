// TextNormalizerTests.cpp — assertions for the geometry-driven normaliser.
//
// This is the one part of SnipText whose failure mode is silent: a broken
// join rule produces text that looks perfectly plausible and is subtly wrong,
// which is exactly the kind of thing nobody catches by eye. The six cases
// below are the ones that shaped the algorithm — prose, a two-paragraph
// block, a UI dialog, a bulleted list, numbered steps, and a hyphenated
// wrap — plus the traps the list-marker rule has to avoid.
//
// No framework: one assert helper and a main. Adding a dependency to test
// two hundred lines of arithmetic would be its own kind of mistake.

#include "TextNormalizer.h"

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks   = 0;

void Check(const wchar_t* name, const std::wstring& actual, const std::wstring& expected) {
    ++g_checks;
    if (actual == expected) return;
    ++g_failures;
    std::wprintf(L"FAIL  %s\n  expected: [%s]\n  actual:   [%s]\n",
                 name, expected.c_str(), actual.c_str());
}

void CheckBool(const wchar_t* name, bool actual, bool expected) {
    ++g_checks;
    if (actual == expected) return;
    ++g_failures;
    std::wprintf(L"FAIL  %s\n  expected: %s\n  actual:   %s\n",
                 name, expected ? L"true" : L"false", actual ? L"true" : L"false");
}

// Builds a line with sane defaults. Geometry is normalised, bottom-left
// origin, so a HIGHER midY is higher on the page.
OcrLine Line(const wchar_t* text, double minX, double maxX, double midY,
             double height = 0.04) {
    OcrLine line;
    line.text   = text;
    line.minX   = minX;
    line.maxX   = maxX;
    line.midY   = midY;
    line.height = height;
    return line;
}

// Successive lines of body text at normal leading, each running to the right
// margin unless told otherwise.
std::vector<OcrLine> Block(const std::vector<std::pair<const wchar_t*, double>>& entries,
                           double leading = 0.06, double leftMargin = 0.1) {
    std::vector<OcrLine> lines;
    double y = 0.9;
    for (const auto& entry : entries) {
        lines.push_back(Line(entry.first, leftMargin, entry.second, y));
        y -= leading;
    }
    return lines;
}

} // namespace

int wmain() {
    // --- prose: a wrapped paragraph becomes one line ---
    {
        const std::vector<OcrLine> lines = Block({
            { L"The quick brown fox jumps over", 0.9 },
            { L"the lazy dog and keeps going",   0.9 },
            { L"until it stops.",                0.4 },   // short: a real break follows
        });
        Check(L"prose joins wrapped lines", text::Normalize(lines, false),
              L"The quick brown fox jumps over the lazy dog and keeps going until it stops.");
    }

    // --- a line that stops short of the margin did not wrap ---
    // The third line is what establishes where the right margin is; without
    // something wide in the block, the widest short line would look like it
    // reached the edge.
    {
        std::vector<OcrLine> lines;
        lines.push_back(Line(L"Priority:", 0.1, 0.35, 0.90));
        lines.push_back(Line(L"High",      0.1, 0.30, 0.84));
        lines.push_back(Line(L"A much longer line of body text here", 0.1, 0.9, 0.78));
        Check(L"short lines keep their break", text::Normalize(lines, false),
              L"Priority:\r\nHigh\r\nA much longer line of body text here");
    }

    // --- an oversized vertical gap is a paragraph break ---
    {
        std::vector<OcrLine> lines;
        lines.push_back(Line(L"First paragraph ends here.", 0.1, 0.9, 0.90));
        // Gap well past 1.6 x the median height.
        lines.push_back(Line(L"Second paragraph starts.",   0.1, 0.9, 0.70));
        Check(L"big gap yields a blank line", text::Normalize(lines, false),
              L"First paragraph ends here.\r\n\r\nSecond paragraph starts.");
    }

    // --- an indented line is not a continuation ---
    {
        std::vector<OcrLine> lines;
        lines.push_back(Line(L"Configuration options are", 0.1, 0.9,  0.90));
        lines.push_back(Line(L"indented under here",       0.35, 0.9, 0.84));
        Check(L"indentation forces a break", text::Normalize(lines, false),
              L"Configuration options are\r\nindented under here");
    }

    // --- bulleted and numbered lists keep their structure ---
    {
        std::vector<OcrLine> lines;
        lines.push_back(Line(L"Steps to follow before you begin", 0.1, 0.9, 0.90));
        lines.push_back(Line(L"1. Open the settings panel",        0.1, 0.9, 0.84));
        lines.push_back(Line(L"2. Choose an output folder",        0.1, 0.9, 0.78));
        lines.push_back(Line(L"• Remember to save",           0.1, 0.9, 0.72));
        Check(L"list markers force breaks", text::Normalize(lines, false),
              L"Steps to follow before you begin\r\n1. Open the settings panel\r\n"
              L"2. Choose an output folder\r\n• Remember to save");
    }

    // --- a word split across a wrap is rejoined ---
    {
        std::vector<OcrLine> lines;
        lines.push_back(Line(L"This is a long word that needs manage-", 0.1, 0.9, 0.90));
        lines.push_back(Line(L"ment attention right now",              0.1, 0.4, 0.84));
        Check(L"de-hyphenates a wrapped word", text::Normalize(lines, false),
              L"This is a long word that needs management attention right now");
    }

    // --- keepLineBreaks bypasses the geometry entirely ---
    {
        const std::vector<OcrLine> lines = Block({
            { L"const int x = 1;", 0.9 },
            { L"const int y = 2;", 0.9 },
        });
        Check(L"keepLineBreaks preserves every line", text::Normalize(lines, true),
              L"const int x = 1;\r\nconst int y = 2;");
    }

    // --- whitespace handling ---
    Check(L"collapses interior runs", text::CollapseWhitespace(L"  a   b \t c  "), L"a b c");
    Check(L"strips zero-width spaces", text::CollapseWhitespace(L"a​b"), L"ab");
    Check(L"empty stays empty", text::CollapseWhitespace(L"   \t  "), L"");

    // --- lines that clean to nothing are dropped, geometry and all ---
    {
        std::vector<OcrLine> lines;
        lines.push_back(Line(L"Only line", 0.1, 0.4, 0.9));
        lines.push_back(Line(L"   ",       0.1, 0.9, 0.84));
        Check(L"blank lines are dropped", text::Normalize(lines, false), L"Only line");
    }

    // --- empty input ---
    Check(L"no lines yields no text", text::Normalize({}, false), L"");
    Check(L"no lines yields no text (keep)", text::Normalize({}, true), L"");

    // --- the list-marker traps ---
    CheckBool(L"bullet with a space is a list item",   text::IsListItem(L"• item"), true);
    CheckBool(L"bare dash is a hyphen, not a bullet",  text::IsListItem(L"-"), false);
    CheckBool(L"dash with no space is a hyphen",       text::IsListItem(L"-x"), false);
    CheckBool(L"dash with a space is a bullet",        text::IsListItem(L"- item"), true);
    CheckBool(L"three digits and a dot is a marker",   text::IsListItem(L"123. Something"), true);
    CheckBool(L"a year is not a list marker",          text::IsListItem(L"2026. Something"), false);
    CheckBool(L"a decimal is not a list marker",       text::IsListItem(L"123.45 total"), false);
    CheckBool(L"a lone marker is still a marker",      text::IsListItem(L"1."), true);
    CheckBool(L"all digits is not a marker",           text::IsListItem(L"123"), false);
    CheckBool(L"a paren marker counts",                text::IsListItem(L"2) Next"), true);
    CheckBool(L"a colon does not",                     text::IsListItem(L"2: Next"), false);
    CheckBool(L"empty is not a list item",             text::IsListItem(L""), false);

    std::wprintf(L"\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
