// Clipboard.h — text clipboard write, with read-back verification.

#pragma once

#include "framework.h"

namespace clipboard {

// Writes text and immediately reads it back, so the log can prove whether the
// write stuck. Empty text is refused outright: OCR coming back blank must
// never clobber whatever the user already had copied.
//
// Returns false when the write was rejected or nothing came back.
bool CopyText(HWND owner, const std::wstring& text);

} // namespace clipboard
