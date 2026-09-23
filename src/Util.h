// Util.h — small shared helpers: strings, paths, time, DPI, geometry.

#pragma once

#include "framework.h"

namespace util {

// --- strings ---------------------------------------------------------------

std::wstring Format(const wchar_t* fmt, ...);
std::string  ToUtf8(const std::wstring& text);
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
// "yyyy-MM-dd HH:mm:ss.SSS" — the log line stamp.
std::wstring LogTimestamp(const SYSTEMTIME& time);

// --- paths -----------------------------------------------------------------

std::wstring KnownFolder(REFKNOWNFOLDERID id, const wchar_t* homeFallback);
std::wstring HomeDirectory();
std::wstring JoinPath(const std::wstring& base, const std::wstring& leaf);
std::wstring LastPathComponent(const std::wstring& path);
std::wstring FileExtensionLower(const std::wstring& path);
bool         EnsureDirectory(const std::wstring& path);
bool         PathExists(const std::wstring& path);
std::wstring ExecutablePath();

// Replaces a leading %USERPROFILE% with "~", the way the macOS original
// abbreviates a home-relative path for display. Menu labels are the only
// place this is used; log lines always carry the absolute path.
std::wstring DisplayPath(const std::wstring& path);

// --- DPI and geometry ------------------------------------------------------

// The process is Per-Monitor-V2 aware (see the manifest), so every coordinate
// the program handles is already in physical pixels and no scaling conversion
// is needed anywhere. This returns the scale only for sizing chrome — menus,
// handles, toolbars — that should stay a constant physical size.
double  DpiScaleForWindow(HWND hwnd);
double  DpiScaleForMonitor(HMONITOR monitor);
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
