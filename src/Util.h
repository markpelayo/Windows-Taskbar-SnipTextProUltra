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
// Used by the recording indicator, so the green frame and the Stop button
// stay out of the video.
bool ExcludeFromCapture(HWND hwnd);
// The inverse: puts a window back into screen captures. Needed because
// exclusion is now a state that can be turned off again — an editor that
// is no longer kept on top has no reason to be invisible to the recorder.
bool IncludeInCapture(HWND hwnd);

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
// Plain member-wise equality. ::EqualRect exists but treats every empty
// rectangle as equal to every other, which is wrong for a cache key.
inline bool RectsEqual(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top &&
           a.right == b.right && a.bottom == b.bottom;
}
inline int RectHeight(const RECT& r) { return r.bottom - r.top; }

double PointSegmentDistance(double px, double py,
                            double ax, double ay,
                            double bx, double by);

} // namespace util
