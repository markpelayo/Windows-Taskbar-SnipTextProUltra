// Ocr.h — on-device text recognition via Windows.Media.Ocr.
//
// Nothing leaves the machine. Windows.Media.Ocr is part of the OS, ships with
// the installed display languages, and needs no model download, no network
// and no third-party library — which is why it was chosen over Tesseract.
//
// The WinRT type is reached through the ABI headers and WRL rather than
// C++/WinRT, so the build needs nothing beyond the Windows SDK: no NuGet
// package, no code generation step, no extra DLL beside the executable.

#pragma once

#include "Bitmap.h"
#include "OcrLine.h"

#include <string>
#include <vector>

namespace ocr {

// Which recogniser to use.
//
// Auto is the default and the one worth understanding: Windows OCR runs
// first because it is fast and right nearly always, and Tesseract runs only
// when that came back nearly empty. That is precisely the case Windows fails
// on — a string with no dictionary word in it, which its lexicon discards
// after reading it perfectly well.
enum class Engine { Auto, WindowsOnly, TesseractOnly };

struct Result {
    std::vector<OcrLine> lines;
    bool                 engineAvailable = true;
    std::wstring         failure;         // empty unless something actually broke
};

// Recognises text and returns visual lines in reading order: top to bottom,
// then left to right within each row.
//
// Safe to call from a worker thread — it initialises the apartment it needs
// and tears it down again.
Result Recognize(const Bitmap& image, Engine engine = Engine::Auto);

// Which engine actually produced the last result, for the log and the menu.
const wchar_t* EngineName(Engine engine);

// True when at least one OCR language is installed. Checked once and cached;
// the answer only changes when the user installs a language pack, which
// requires a sign-out anyway.
bool IsAvailable();

} // namespace ocr
