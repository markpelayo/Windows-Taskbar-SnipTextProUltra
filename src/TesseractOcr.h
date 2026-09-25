// TesseractOcr.h — the second OCR engine, for text that isn't words.
//
// Why a second engine exists at all:
//
// Windows.Media.Ocr is a *language* recogniser. It scores what it reads
// against a lexicon, and that has two consequences neither of which can be
// switched off:
//
//   * A region with no dictionary word in it can be read perfectly and then
//     discarded. "@#4!TW$RH^%&CFG?:" comes back as nothing at all.
//   * When confidence is low it REWRITES characters to make a word. That is
//     why a serial number comes back with the wrong digit — the engine is
//     not misreading it, it is correcting it.
//
// Tesseract has explicit switches for both (`load_system_dawg` and
// `load_freq_dawg`), so with the dictionary off it reports what it actually
// saw. That is the whole reason it is here.
//
// Cost, and how it is kept down:
//
//   * The engine is built ONLY when SNIPTEXT_WITH_TESSERACT is defined. The
//     no-dependency build.bat path compiles this file to nothing.
//   * Nothing is loaded at startup. The API object is created per capture and
//     destroyed with it, so idle memory is unchanged — the program still
//     holds nothing but a message loop, its hotkeys and a tray icon.
//     Re-initialising costs ~100 ms, which is the right trade for a path that
//     only runs when the fast engine came back nearly empty.
//   * The trained model is embedded in the executable as a resource, so there
//     is still one file, still nothing to install, and still no network.

#pragma once

#include "Bitmap.h"
#include "OcrLine.h"

#include <string>
#include <vector>

namespace tesseract_ocr {

// True when this build has the engine compiled in at all.
bool IsCompiledIn();

// True when the engine is compiled in AND its embedded model loaded. Checked
// once and cached; a missing model is a build problem, not a runtime one.
bool IsAvailable();

struct Result {
    std::vector<OcrLine> lines;
    std::wstring         failure;   // empty unless something actually broke
};

// Recognises with the dictionary disabled, so the output is what was on
// screen rather than what the nearest word would have been.
//
// Safe to call from a worker thread. Returns an empty result rather than
// throwing if the engine is not present in this build.
Result Recognize(const Bitmap& image);

} // namespace tesseract_ocr
