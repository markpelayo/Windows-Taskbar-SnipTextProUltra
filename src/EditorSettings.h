// EditorSettings.h — the annotation editor's persisted tool, colour and
// stroke width.
//
// Your settings carry between editor windows: set the slider to maximum once
// and the next screenshot opens with it.

#pragma once

#include "Annotation.h"
#include "framework.h"

namespace editor_settings {

// The nine preset colours offered by the swatch grid, in grid order.
extern const COLORREF kPresetColours[9];
constexpr COLORREF kDefaultColour = RGB(52, 199, 89);   // #34C759

// Stroke width is in image pixels, so the default has to scale with the
// display: a 4-pixel stroke on a 200% monitor is a hairline.
double DefaultLineWidth();

double LineWidth();
void   SetLineWidth(double width);

COLORREF Colour();
void     SetColour(COLORREF colour);

Tool CurrentTool();
void SetTool(Tool tool);

// Compares VALUES, not key presence. Key presence looked simpler and was
// wrong: the editor writes these whenever a window opens or the user
// re-picks the tool they already had, so "a key exists" stopped meaning
// "the user changed something" — which left Sanitize permanently enabled.
bool IsDefault();
void RestoreDefaults();

} // namespace editor_settings
