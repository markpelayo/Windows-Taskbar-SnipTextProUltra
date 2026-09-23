// OcrLine.h — one visual line of recognised text, plus the geometry the
// normaliser reasons about.
//
// Coordinates are normalised to 0...1 with a BOTTOM-LEFT origin, matching the
// macOS original. Windows OCR reports top-left pixel rectangles, so the
// conversion happens once, in OcrService, before any geometry is used —
// the row-bucketing tolerance is expressed in normalised height units and
// would be meaningless against pixels.
//
// Note the three different reductions in one struct: X is a union
// (min of mins, max of maxes), height is a max, and midY is a *mean*. That
// asymmetry is deliberate — see ARCHITECTURE.md.

#pragma once

#include <string>
#include <vector>

struct OcrLine {
    std::wstring text;
    double       minX   = 0.0;   // 0 = left edge of the image
    double       maxX   = 0.0;   // 1 = right edge
    double       midY   = 0.0;   // 0 = bottom, 1 = top. Higher is higher on screen.
    double       height = 0.0;   // fraction of image height
};
