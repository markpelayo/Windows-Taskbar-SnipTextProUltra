// Util.h — small shared helpers: strings, paths, time, DPI, geometry.

#pragma once

#include "framework.h"

namespace util {

// --- strings ---------------------------------------------------------------

std::wstring Format(const wchar_t* fmt, ...);
std::wstring FromUtf8(const std::string& text);

// Decodes UTF-16 into code points. The normaliser and the menu-preview
// truncation both count user-perceived characters, and counting UTF-16 units
// would cut a surrogate pair in half.
std::vector<char32_t> ToCodePoints(const std::wstring& text);
std::wstring          FromCodePoints(const std::vector<char32_t>& points);

// Unicode predicates. These deliberately do not use <cctype>, which is
// byte-oriented and would diverge on non-Latin input and on U+00A0.
bool IsUnicodeWhitespace(char32_t c);
bool IsUnicodeDigit(char32_t c);
bool IsUnicodeLowercase(char32_t c);

// --- time ------------------------------------------------------------------

// "yyyy-MM-dd at HH.mm.ss.SSS" in local time, never locale-sensitive.
// Milliseconds are in the name because two captures in the same second would
// otherwise overwrite each other.
std::wstring FileNameTimestamp();

// --- paths -----------------------------------------------------------------

std::wstring KnownFolder(REFKNOWNFOLDERID id, const wchar_t* homeFallback);
std::wstring HomeDirectory();
std::wstring JoinPath(const std::wstring& base, const std::wstring& leaf);
std::wstring LastPathComponent(const std::wstring& path);
std::wstring FileExtensionLower(const std::wstring& path);
bool         EnsureDirectory(const std::wstring& path);
std::wstring ExecutablePath();

// Replaces a leading %USERPROFILE% with "~", the way the macOS original
// abbreviates a home-relative path for display. Menu labels are the only
// place this is used.
std::wstring DisplayPath(const std::wstring& path);

// --- keeping our own windows out of captures -------------------------------
//
// SetWindowDisplayAffinity with WDA_EXCLUDEFROMCAPTURE, which the Windows
// documentation describes for exactly this purpose: "windows that show video
// recording controls, so that the controls are not included in the capture."
// The window keeps rendering on the physical monitor and disappears from
// anything that captures the screen.
//
// Windows 10 version 2004 and later. Returns false on anything older — and
// the affinity is read back rather than inferred from the BOOL, because
// WDA_EXCLUDEFROMCAPTURE is WDA_MONITOR plus a bit, so an older build could
// accept the call and black the window out instead of removing it.
//
// Two callers, for two reasons: the recording indicator, so the frame and
// Stop button stay out of the video; and the capture path, so a menu that is
// still fading out stays out of the screenshot.
bool ExcludeFromCapture(HWND hwnd);

// Makes sure our own flyout menu is not in the capture. See the long note on
// the definition for why this ended up being a wait rather than anything
// cleverer — it is measured, not assumed.
//
// Returns how many menu windows it found, which a diagnostic build showed is
// always zero in practice. Kept because it costs nothing and a future Windows
// could behave differently.
int SuppressOwnMenusForCapture();

// Marks our flyout menu as having just been dismissed, for the lifetime of the
// object. Construct one around the dispatch of a menu command.
//
// The capture path cannot otherwise tell a menu-initiated capture from a
// hotkey one, and the difference decides whether it has to wait for a menu
// fade at all: a hotkey never showed a menu, so it must never pay for one.
class MenuDismissGuard {
public:
    MenuDismissGuard();
    ~MenuDismissGuard();
    MenuDismissGuard(const MenuDismissGuard&) = delete;
    MenuDismissGuard& operator=(const MenuDismissGuard&) = delete;
};


// --- DPI and geometry ------------------------------------------------------

// The process is Per-Monitor-V2 aware (see the manifest), so every coordinate
// the program handles is already in physical pixels and no scaling conversion
// is needed anywhere. Chrome sizes come from fixed constants, which is why
// there are no DPI-scale helpers here.
HMONITOR MonitorUnderCursor();
RECT    MonitorBounds(HMONITOR monitor);

inline RECT MakeRect(int left, int top, int right, int bottom) {
    RECT r{ left, top, right, bottom };
    return r;
}
// Two dragged corners to a rect with positive width and height.
RECT NormalizedRect(POINT a, POINT b);
RECT InflateRect(const RECT& r, int dx, int dy);
RECT UnionRect(const RECT& a, const RECT& b);
bool RectContains(const RECT& r, POINT p);
inline int RectWidth(const RECT& r)  { return r.right - r.left; }
inline int RectHeight(const RECT& r) { return r.bottom - r.top; }

double PointSegmentDistance(double px, double py,
                            double ax, double ay,
                            double bx, double by);

} // namespace util
