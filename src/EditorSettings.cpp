#include "EditorSettings.h"

#include "Settings.h"
#include "Util.h"

#include <cstdio>

namespace editor_settings {

// Row one, then row two. The tenth cell in the grid is the colour wheel,
// which has no preset of its own.
const COLORREF kPresetColours[9] = {
    RGB(255,  59,  48),   // red
    RGB(  0, 122, 255),   // blue
    RGB(255, 149,   0),   // orange
    RGB(  0,   0,   0),   // black
    RGB(255, 255, 255),   // white
    RGB(255,   0, 255),   // magenta
    RGB( 52, 199,  89),   // green
    RGB(255, 204,   0),   // yellow
    RGB(142, 142, 147),   // grey
};

double DefaultLineWidth() {
    // Physical pixels, so this tracks the primary monitor's scaling.
    UINT dpi = ::GetDpiForSystem();
    if (dpi == 0) dpi = 96;
    return 4.0 * (static_cast<double>(dpi) / 96.0);
}

double LineWidth() {
    const double stored = settings::GetDouble(settings::key::kEditorLineWidth, 0.0);
    // Zero is not a representable stroke width, so it doubles as "absent".
    if (stored <= 0.0) return DefaultLineWidth();
    return (std::min)(60.0, stored);
}

void SetLineWidth(double width) {
    settings::SetDouble(settings::key::kEditorLineWidth, width);
}

COLORREF Colour() {
    // Stored as plain R,G,B text rather than a packed integer, so the
    // registry stays readable and a hand-edited value still parses.
    const std::wstring stored = settings::GetString(settings::key::kEditorColor);
    if (stored.empty()) return kDefaultColour;

    int r = 0, g = 0, b = 0;
    if (::swscanf_s(stored.c_str(), L"%d,%d,%d", &r, &g, &b) != 3) return kDefaultColour;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return kDefaultColour;
    return RGB(r, g, b);
}

void SetColour(COLORREF colour) {
    settings::SetString(settings::key::kEditorColor,
                        util::Format(L"%d,%d,%d",
                                     GetRValue(colour), GetGValue(colour), GetBValue(colour)));
}

Tool CurrentTool() {
    return ToolFromKeyValue(settings::GetString(settings::key::kEditorTool, L"arrow"));
}

void SetTool(Tool tool) {
    settings::SetString(settings::key::kEditorTool, ToolKeyValue(tool));
}

bool IsDefault() {
    return std::fabs(LineWidth() - DefaultLineWidth()) < 0.001
        && CurrentTool() == Tool::Arrow
        && Colour() == kDefaultColour;
}

void RestoreDefaults() {
    settings::Remove(settings::key::kEditorLineWidth);
    settings::Remove(settings::key::kEditorColor);
    settings::Remove(settings::key::kEditorTool);
}

} // namespace editor_settings
