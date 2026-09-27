#include "Util.h"

#include <knownfolders.h>
#include <cstdarg>
#include <cstdio>
#include <cwctype>

namespace util {

// --- strings ---------------------------------------------------------------

std::wstring Format(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = _vscwprintf(fmt, args);
    va_end(args);
    if (needed <= 0) return std::wstring();

    std::wstring out(static_cast<size_t>(needed), L'\0');
    va_start(args, fmt);
    _vsnwprintf_s(&out[0], static_cast<size_t>(needed) + 1, _TRUNCATE, fmt, args);
    va_end(args);
    return out;
}

std::wstring FromUtf8(const std::string& text) {
    if (text.empty()) return std::wstring();
    int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], needed);
    return out;
}

std::vector<char32_t> ToCodePoints(const std::wstring& text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t unit = text[i];
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < text.size()) {
            wchar_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                out.push_back(0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10)
                                      + (static_cast<char32_t>(low) - 0xDC00));
                ++i;
                continue;
            }
        }
        out.push_back(static_cast<char32_t>(unit));
    }
    return out;
}

std::wstring FromCodePoints(const std::vector<char32_t>& points) {
    std::wstring out;
    out.reserve(points.size());
    for (char32_t c : points) {
        if (c < 0x10000) {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            char32_t v = c - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (v & 0x3FF)));
        }
    }
    return out;
}

bool IsUnicodeWhitespace(char32_t c) {
    switch (c) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
    case 0x20: case 0x85: case 0xA0:
    case 0x1680:
    case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
    case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
    case 0x200A:
    case 0x2028: case 0x2029:
    case 0x202F: case 0x205F: case 0x3000:
        return true;
    default:
        return false;
    }
}

bool IsUnicodeDigit(char32_t c) {
    if (c < 0x10000) {
        WORD type = 0;
        wchar_t unit = static_cast<wchar_t>(c);
        if (::GetStringTypeW(CT_CTYPE1, &unit, 1, &type)) return (type & C1_DIGIT) != 0;
    }
    return c >= U'0' && c <= U'9';
}

bool IsUnicodeLowercase(char32_t c) {
    if (c < 0x10000) {
        WORD type = 0;
        wchar_t unit = static_cast<wchar_t>(c);
        if (::GetStringTypeW(CT_CTYPE1, &unit, 1, &type)) return (type & C1_LOWER) != 0;
    }
    return false;
}

// --- time ------------------------------------------------------------------

std::wstring FileNameTimestamp() {
    SYSTEMTIME t{};
    ::GetLocalTime(&t);
    return Format(L"%04d-%02d-%02d at %02d.%02d.%02d.%03d",
                  t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}

// --- paths -----------------------------------------------------------------

std::wstring HomeDirectory() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &raw)) && raw) {
        std::wstring out(raw);
        ::CoTaskMemFree(raw);
        return out;
    }
    wchar_t buffer[MAX_PATH]{};
    DWORD length = ::GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    return (length > 0 && length < MAX_PATH) ? std::wstring(buffer) : std::wstring(L"C:\\");
}

std::wstring KnownFolder(REFKNOWNFOLDERID id, const wchar_t* homeFallback) {
    PWSTR raw = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) {
        std::wstring out(raw);
        ::CoTaskMemFree(raw);
        return out;
    }
    return JoinPath(HomeDirectory(), homeFallback);
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& leaf) {
    if (base.empty()) return leaf;
    if (leaf.empty())  return base;
    std::wstring out = base;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    size_t start = 0;
    while (start < leaf.size() && (leaf[start] == L'\\' || leaf[start] == L'/')) ++start;
    out.append(leaf, start, std::wstring::npos);
    return out;
}

std::wstring LastPathComponent(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return path;
    // A trailing separator would otherwise yield an empty component.
    if (cut + 1 == path.size()) {
        std::wstring trimmed = path.substr(0, cut);
        return LastPathComponent(trimmed);
    }
    return path.substr(cut + 1);
}

std::wstring FileExtensionLower(const std::wstring& path) {
    std::wstring name = LastPathComponent(path);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= name.size()) return std::wstring();
    std::wstring ext = name.substr(dot + 1);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(::towlower(c));
    return ext;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    int result = ::SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD written = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return std::wstring();
        if (written < buffer.size() - 1) return std::wstring(buffer.data(), written);
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring DisplayPath(const std::wstring& path) {
    const std::wstring home = HomeDirectory();
    if (!home.empty() && path.size() >= home.size() &&
        ::CompareStringOrdinal(path.c_str(), static_cast<int>(home.size()),
                               home.c_str(), static_cast<int>(home.size()), TRUE) == CSTR_EQUAL) {
        return L"~" + path.substr(home.size());
    }
    return path;
}

// --- DPI and geometry ------------------------------------------------------

// --- keeping our own windows out of captures -------------------------------

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

bool ExcludeFromCapture(HWND hwnd) {
    if (!hwnd) return false;

    // Resolved dynamically. Both functions have existed in user32 since
    // Windows 7, but importing them statically would make the program refuse
    // to start on anything older to buy a cosmetic feature.
    using SetAffinity = BOOL (WINAPI*)(HWND, DWORD);
    using GetAffinity = BOOL (WINAPI*)(HWND, DWORD*);
    static SetAffinity setAffinity = nullptr;
    static GetAffinity getAffinity = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
            setAffinity = reinterpret_cast<SetAffinity>(
                ::GetProcAddress(user32, "SetWindowDisplayAffinity"));
            getAffinity = reinterpret_cast<GetAffinity>(
                ::GetProcAddress(user32, "GetWindowDisplayAffinity"));
        }
    }
    if (!setAffinity || !getAffinity) return false;
    if (!setAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) return false;

    // Read back and insist on the exact value. WDA_EXCLUDEFROMCAPTURE is
    // 0x11, which is WDA_MONITOR (0x01) with an extra bit, so a build that
    // does not know the newer flag could accept the call and apply
    // WDA_MONITOR instead — which blacks the window out of the capture rather
    // than removing it. That is worse than not trying.
    DWORD applied = 0;
    return getAffinity(hwnd, &applied) && applied == WDA_EXCLUDEFROMCAPTURE;
}

namespace {

// Windows' own class for a popup menu. Menus raised by TrackPopupMenuEx
// belong to the thread that raised them, so our own menus are findable this
// way and nobody else's are.
constexpr const wchar_t* kMenuWindowClass = L"#32768";

// How long to wait for a fading menu, at most. The Windows menu fade is on
// the order of 200ms; the ceiling exists so a wedged menu can never hang a
// capture, not because the wait is expected to reach it.
constexpr ULONGLONG kFadeCeilingMs = 300;

// A final settle after the window has gone, for the DWM animation of its last
// rendered frame. Deliberately small: it is paid on every capture on a
// machine with the effect enabled.
constexpr DWORD kFadeSettleMs = 30;

MenuSuppressionReport g_report;

BOOL CALLBACK SuppressMenuWindow(HWND hwnd, LPARAM parameter) {
    wchar_t className[32]{};
    if (::GetClassNameW(hwnd, className, 32) == 0) return TRUE;
    if (::wcscmp(className, kMenuWindowClass) != 0) return TRUE;

    // NO IsWindowVisible check, and its absence is the point.
    //
    // There used to be one here, added as an obvious optimisation: why bother
    // with a window that is not on screen? Because during a fade-out it very
    // likely IS one of those. Windows appears to hide the menu window and let
    // DWM animate the last surface it rendered, so the window that still
    // needs excluding reports itself invisible — and that guard skipped
    // exactly the case the whole function exists for. It is the reason this
    // did not work the first time.
    if (ExcludeFromCapture(hwnd)) {
        ++g_report.excluded;
    } else {
        ::ShowWindow(hwnd, SW_HIDE);
        ++g_report.hidden;
    }

    ++*reinterpret_cast<int*>(parameter);
    return TRUE;
}

// Whether Windows is set to fade menus out after a click — Performance
// Options > Visual Effects > "Fade out menu items after clicking". On by
// default.
//
// Asked because it decides whether waiting is worth anything at all. With the
// effect off there is nothing to wait for and a capture must not pay a
// millisecond; with it on, the wait below is the only thing that can work if
// the menu window has already been destroyed and what remains is a DWM
// animation of its last frame — which no amount of excluding or hiding a
// window can touch.
bool MenusFadeOut() {
    BOOL fade = FALSE;
    if (!::SystemParametersInfoW(SPI_GETMENUFADE, 0, &fade, 0)) return false;
    return fade != FALSE;
}

BOOL CALLBACK CountMenuWindow(HWND hwnd, LPARAM parameter) {
    wchar_t className[32]{};
    if (::GetClassNameW(hwnd, className, 32) == 0) return TRUE;
    if (::wcscmp(className, kMenuWindowClass) == 0) {
        ++*reinterpret_cast<int*>(parameter);
    }
    return TRUE;
}

int OwnMenuWindowCount() {
    int found = 0;
    ::EnumThreadWindows(::GetCurrentThreadId(), &CountMenuWindow,
                        reinterpret_cast<LPARAM>(&found));
    return found;
}

} // namespace

int SuppressOwnMenusForCapture() {
    // Why this is needed at all, since TrackPopupMenuEx has already returned
    // and the HMENU has already been destroyed:
    //
    // Windows has a setting — Performance Options → Visual Effects → "Fade
    // out menu items after clicking" — which is ON by default. With it on,
    // the menu WINDOW outlives the selection and fades over roughly 200ms.
    // The menu is logically gone and visually still there.
    //
    // That is why waiting for the compositor was not enough: DwmFlush does
    // what it promises and hands back a frame that faithfully contains a
    // half-faded menu. There is nothing to wait for, because the thing has
    // not begun to disappear.
    //
    // Three things happen here, cheapest first, because the first two may well
    // be enough and neither costs anything measurable:
    //
    //   1. Take any menu window of ours out of the capture, or hide it. Free,
    //      instant, and correct whenever a window still exists to act on.
    //   2. If — and only if — Windows says it fades menus out, wait for that
    //      window to go away. This returns the moment it does, so it costs
    //      the fade and nothing more, and machines with the effect switched
    //      off never reach it.
    //   3. A short bounded settle for the case no window handle can reach at
    //      all: the menu already destroyed, DWM still dissolving the surface
    //      it last rendered. Nothing can be excluded or hidden there, so this
    //      is the only thing left — which is why it exists despite a delay
    //      being the shape of fix I wanted to avoid.
    //
    // A blanket delay is still wrong, and this is not one: it is gated on the
    // system setting that causes the problem, so the cost falls only on the
    // machines that have it.
    g_report = MenuSuppressionReport{};

    int found = 0;
    ::EnumThreadWindows(::GetCurrentThreadId(), &SuppressMenuWindow,
                        reinterpret_cast<LPARAM>(&found));
    g_report.windowsFound = found;
    g_report.fadeEnabled  = MenusFadeOut();

    // Everything above is free and instant. What follows costs time, so it
    // only runs when Windows says it is fading menus out — the machines that
    // have the effect switched off pay nothing whatsoever, which was the
    // whole constraint.
    if (!g_report.fadeEnabled) return found;

    // Wait for the menu window to go away, rather than for a guessed
    // duration. Returns the moment it does, so this costs exactly the fade
    // and not a millisecond more.
    //
    // The pump matters: the menu window belongs to THIS thread, so if USER32
    // drives the fade from a timer here, a plain Sleep would stall the very
    // animation being waited on and the ceiling would always be hit. Paint
    // and sent messages only — dispatching posted messages would include
    // WM_HOTKEY and could re-enter the capture already in progress.
    const ULONGLONG started  = ::GetTickCount64();
    const ULONGLONG deadline = started + kFadeCeilingMs;
    while (OwnMenuWindowCount() > 0 && ::GetTickCount64() < deadline) {
        MSG message;
        while (::PeekMessageW(&message, nullptr, 0, 0,
                              PM_REMOVE | PM_QS_PAINT | PM_QS_SENDMESSAGE)) {
            ::DispatchMessageW(&message);
        }
        ::Sleep(4);
        ++g_report.pollCount;
    }
    g_report.waitedMs         = static_cast<int>(::GetTickCount64() - started);
    g_report.windowsAfterWait = OwnMenuWindowCount();

    // And a short settle for the case no window handle can reach: the menu
    // destroyed, DWM still dissolving the surface it last rendered. Bounded,
    // and still only on machines that have the effect on.
    ::Sleep(kFadeSettleMs);
    return found;
}

const MenuSuppressionReport& LastMenuSuppressionReport() { return g_report; }

HMONITOR MonitorUnderCursor() {
    POINT cursor{};
    ::GetCursorPos(&cursor);
    return ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
}

RECT MonitorBounds(HMONITOR monitor) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && ::GetMonitorInfoW(monitor, &info)) return info.rcMonitor;
    RECT fallback{ 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
    return fallback;
}

RECT NormalizedRect(POINT a, POINT b) {
    RECT r{};
    r.left   = (std::min)(a.x, b.x);
    r.top    = (std::min)(a.y, b.y);
    r.right  = (std::max)(a.x, b.x);
    r.bottom = (std::max)(a.y, b.y);
    return r;
}

RECT InflateRect(const RECT& r, int dx, int dy) {
    RECT out = r;
    out.left   -= dx;
    out.top    -= dy;
    out.right  += dx;
    out.bottom += dy;
    return out;
}

RECT UnionRect(const RECT& a, const RECT& b) {
    RECT out{};
    out.left   = (std::min)(a.left, b.left);
    out.top    = (std::min)(a.top, b.top);
    out.right  = (std::max)(a.right, b.right);
    out.bottom = (std::max)(a.bottom, b.bottom);
    return out;
}

bool RectContains(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

double PointSegmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0001) return std::hypot(px - ax, py - ay);

    double t = ((px - ax) * dx + (py - ay) * dy) / lengthSquared;
    t = (std::max)(0.0, (std::min)(1.0, t));
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

} // namespace util
