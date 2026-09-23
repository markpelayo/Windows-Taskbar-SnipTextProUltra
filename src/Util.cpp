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

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                          &out[0], needed, nullptr, nullptr);
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

std::wstring LogTimestamp(const SYSTEMTIME& t) {
    return Format(L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
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

bool PathExists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
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

double DpiScaleForWindow(HWND hwnd) {
    UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : ::GetDpiForSystem();
    if (dpi == 0) dpi = 96;
    return static_cast<double>(dpi) / 96.0;
}

double DpiScaleForMonitor(HMONITOR monitor) {
    UINT dpiX = 96, dpiY = 96;
    if (monitor && SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX) {
        return static_cast<double>(dpiX) / 96.0;
    }
    return 1.0;
}

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
