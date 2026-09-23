// framework.h — common includes and small shared types.
//
// One header, included first by every translation unit, so the Windows
// configuration macros are set in exactly one place.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Windows 10 1903 is the floor: GetDpiForWindow, Per-Monitor-V2 and the
// Windows.Media.Ocr engine all predate it comfortably.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include <objbase.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// RAII helpers. Every GDI object, handle and COM pointer in this program is
// owned by one of these, so there is no path — including an early return or a
// thrown exception — on which a resource leaks. A tool that is left running
// for weeks cannot afford a leak on any branch.
// ---------------------------------------------------------------------------

template <typename T, typename Deleter>
class Scoped {
public:
    Scoped() = default;
    explicit Scoped(T value) : value_(value) {}
    Scoped(const Scoped&) = delete;
    Scoped& operator=(const Scoped&) = delete;
    Scoped(Scoped&& other) noexcept : value_(other.value_) { other.value_ = T{}; }
    Scoped& operator=(Scoped&& other) noexcept {
        if (this != &other) { reset(); value_ = other.value_; other.value_ = T{}; }
        return *this;
    }
    ~Scoped() { reset(); }

    void reset(T value = T{}) {
        if (value_) Deleter{}(value_);
        value_ = value;
    }
    T get() const { return value_; }
    T release() { T v = value_; value_ = T{}; return v; }
    explicit operator bool() const { return value_ != T{}; }
    T* put() { reset(); return &value_; }

private:
    T value_{};
};

struct GdiObjectDeleter { void operator()(HGDIOBJ o) const { ::DeleteObject(o); } };
struct DcDeleter        { void operator()(HDC dc) const { ::DeleteDC(dc); } };
struct HandleDeleter    { void operator()(HANDLE h) const { if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h); } };
struct IconDeleter      { void operator()(HICON i) const { ::DestroyIcon(i); } };
struct MenuDeleter      { void operator()(HMENU m) const { ::DestroyMenu(m); } };
struct LocalFreeDeleter { void operator()(void* p) const { ::LocalFree(p); } };
struct CoTaskFreeDeleter{ void operator()(void* p) const { ::CoTaskMemFree(p); } };

using ScopedBitmap = Scoped<HBITMAP, GdiObjectDeleter>;
using ScopedBrush  = Scoped<HBRUSH, GdiObjectDeleter>;
using ScopedPen    = Scoped<HPEN, GdiObjectDeleter>;
using ScopedFont   = Scoped<HFONT, GdiObjectDeleter>;
using ScopedRegion = Scoped<HRGN, GdiObjectDeleter>;
using ScopedDC     = Scoped<HDC, DcDeleter>;
using ScopedHandle = Scoped<HANDLE, HandleDeleter>;
using ScopedIcon   = Scoped<HICON, IconDeleter>;
using ScopedMenu   = Scoped<HMENU, MenuDeleter>;

// Handles from CreateFile and FindFirstFile report failure as
// INVALID_HANDLE_VALUE, not null — so a plain null test on one of those reads
// a failed call as success and hands (HANDLE)-1 to the next API. These two
// types test for both, and FindClose is not CloseHandle: closing a find
// handle with the wrong function is an invalid-handle fault under the debug
// heap.
struct FindDeleter { void operator()(HANDLE h) const { ::FindClose(h); } };

template <typename Deleter>
class ScopedFileHandle {
public:
    ScopedFileHandle() = default;
    explicit ScopedFileHandle(HANDLE value) : value_(value) {}
    ScopedFileHandle(const ScopedFileHandle&) = delete;
    ScopedFileHandle& operator=(const ScopedFileHandle&) = delete;
    ScopedFileHandle(ScopedFileHandle&& other) noexcept : value_(other.value_) {
        other.value_ = INVALID_HANDLE_VALUE;
    }
    ScopedFileHandle& operator=(ScopedFileHandle&& other) noexcept {
        if (this != &other) { reset(); value_ = other.value_; other.value_ = INVALID_HANDLE_VALUE; }
        return *this;
    }
    ~ScopedFileHandle() { reset(); }

    void reset(HANDLE value = INVALID_HANDLE_VALUE) {
        if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) Deleter{}(value_);
        value_ = value;
    }
    HANDLE get() const { return value_; }
    explicit operator bool() const {
        return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
    }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

using ScopedFile = ScopedFileHandle<HandleDeleter>;
using ScopedFind = ScopedFileHandle<FindDeleter>;

// A window DC obtained with GetDC, which must be released rather than deleted.
class WindowDC {
public:
    explicit WindowDC(HWND hwnd) : hwnd_(hwnd), dc_(::GetDC(hwnd)) {}
    WindowDC(const WindowDC&) = delete;
    WindowDC& operator=(const WindowDC&) = delete;
    ~WindowDC() { if (dc_) ::ReleaseDC(hwnd_, dc_); }
    HDC get() const { return dc_; }
    explicit operator bool() const { return dc_ != nullptr; }
private:
    HWND hwnd_;
    HDC  dc_;
};

// Selects an object into a DC and puts the previous one back on scope exit.
class SelectGuard {
public:
    SelectGuard(HDC dc, HGDIOBJ obj) : dc_(dc), prev_(::SelectObject(dc, obj)) {}
    SelectGuard(const SelectGuard&) = delete;
    SelectGuard& operator=(const SelectGuard&) = delete;
    ~SelectGuard() { if (dc_ && prev_) ::SelectObject(dc_, prev_); }
private:
    HDC     dc_;
    HGDIOBJ prev_;
};

// A minimal critical-section wrapper. Used only where a value genuinely
// crosses threads; everything else in this program is main-thread-only by
// construction, which is cheaper than locking and easier to reason about.
class Lock {
public:
    Lock() { ::InitializeCriticalSectionEx(&cs_, 2000, 0); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    ~Lock() { ::DeleteCriticalSection(&cs_); }
    void enter() { ::EnterCriticalSection(&cs_); }
    void leave() { ::LeaveCriticalSection(&cs_); }
private:
    CRITICAL_SECTION cs_{};
};

class LockGuard {
public:
    explicit LockGuard(Lock& lock) : lock_(lock) { lock_.enter(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    ~LockGuard() { lock_.leave(); }
private:
    Lock& lock_;
};
