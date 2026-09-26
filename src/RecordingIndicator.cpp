#include "RecordingIndicator.h"

#include "Log.h"
#include "Util.h"

namespace {

constexpr const wchar_t* kFrameClass = L"SnipTextRecordingFrame";
constexpr const wchar_t* kPillClass  = L"SnipTextRecordingPill";

// Thick enough to be unmissable in peripheral vision, thin enough not to
// swallow the content next to the recorded area.
constexpr int kBorderThickness = 4;
constexpr int kDashLength      = 16;
constexpr int kDashGap         = 10;

constexpr COLORREF kGreen      = RGB(0, 220, 80);
constexpr COLORREF kRedBright  = RGB(255, 45, 45);
constexpr COLORREF kRedDim     = RGB(130, 26, 26);

constexpr int kPillHeight  = 34;
constexpr int kPillPadding = 12;
constexpr int kPillGap     = 8;   // between the frame and the pill
constexpr int kPillDotGap  = 8;   // between the dot and the text

// --- keeping the indicator out of the recording ----------------------------
//
// The region frame solves this geometrically: it is drawn OUTSIDE the recorded
// rectangle, so it cannot be captured. Full screen has no outside, which is
// why there was no frame during a full-screen recording at all — the window
// was created, positioned off the edge of the desktop, and never seen.
//
// SetWindowDisplayAffinity with WDA_EXCLUDEFROMCAPTURE is the mechanism
// Windows provides for precisely this, and the documentation names this exact
// use case: "windows that show video recording controls, so that the controls
// are not included in the capture." The window keeps rendering on the physical
// monitor and disappears from anything that captures the screen.
//
// Windows 10 version 2004 (build 19041) and later. On anything older the call
// fails, and the caller has to fall back to the geometric guarantee rather
// than assume it worked — a frame we *think* is excluded but is not would be
// burned into every recording.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

bool ExcludeFromCapture(HWND hwnd) {
    if (!hwnd) return false;
    // Resolved dynamically. The function has existed in user32 since Windows 7,
    // but importing it statically would make the whole program refuse to start
    // on anything older, to buy a cosmetic feature — and this program has no
    // other reason to require a particular build.
    using SetAffinity = BOOL (WINAPI*)(HWND, DWORD);
    static SetAffinity setAffinity = []() -> SetAffinity {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<SetAffinity>(
                            ::GetProcAddress(user32, "SetWindowDisplayAffinity"))
                      : nullptr;
    }();
    if (!setAffinity) return false;
    if (!setAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) return false;

    // Read it back, and insist on the exact value. WDA_EXCLUDEFROMCAPTURE is
    // 0x11, which is WDA_MONITOR (0x01) with an extra bit set, so a build that
    // does not know the newer flag could plausibly accept the call and apply
    // WDA_MONITOR instead — which blacks the window out of the capture rather
    // than removing it, putting a black band in the video. That is the exact
    // outcome the fallback exists to avoid, so it is not worth inferring from
    // a BOOL.
    using GetAffinity = BOOL (WINAPI*)(HWND, DWORD*);
    static GetAffinity getAffinity = []() -> GetAffinity {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<GetAffinity>(
                            ::GetProcAddress(user32, "GetWindowDisplayAffinity"))
                      : nullptr;
    }();
    if (!getAffinity) return false;

    DWORD applied = 0;
    return getAffinity(hwnd, &applied) && applied == WDA_EXCLUDEFROMCAPTURE;
}

HFONT PillFont() {
    static HFONT font = nullptr;
    if (!font) {
        LOGFONTW description{};
        description.lfHeight  = -16;
        description.lfWeight  = FW_SEMIBOLD;
        description.lfQuality = CLEARTYPE_QUALITY;
        ::wcscpy_s(description.lfFaceName, L"Segoe UI");
        font = ::CreateFontIndirectW(&description);
    }
    return font;
}

void FillSolid(HDC dc, const RECT& rect, COLORREF colour) {
    ScopedBrush brush(::CreateSolidBrush(colour));
    if (brush) ::FillRect(dc, &rect, brush.get());
}

// Builds a window region made of the dashes themselves, for a frame window
// of `width` x `height` whose border is `t` thick.
//
// Shaping the window to the dashes rather than to the border band is what
// makes the frame correct rather than merely nearly right. A window shaped to
// the band would still own the pixels in the gaps between dashes, and those
// pixels have to be painted with something — there is no "transparent" to
// paint. This way every pixel the window owns is green, so there is nothing
// to erase, nothing to composite, and no gap showing whatever was in video
// memory.
//
// It also means the window overlaps nothing at all: not the recorded area,
// and not the gaps either.
ScopedRegion BuildDashRegion(int width, int height, int t) {
    ScopedRegion result(::CreateRectRgn(0, 0, 0, 0));
    if (!result) return result;

    auto add = [&result](int left, int top, int right, int bottom) {
        ScopedRegion piece(::CreateRectRgn(left, top, right, bottom));
        if (piece) ::CombineRgn(result.get(), result.get(), piece.get(), RGN_OR);
    };

    const int stride = kDashLength + kDashGap;
    for (int x = 0; x < width; x += stride) {
        const int end = (std::min)(x + kDashLength, width);
        add(x, 0, end, t);
        add(x, height - t, end, height);
    }
    for (int y = 0; y < height; y += stride) {
        const int end = (std::min)(y + kDashLength, height);
        add(0, y, t, end);
        add(width - t, y, width, end);
    }

    // Solid corners, so the rectangle reads as a rectangle even when a dash
    // happens to land badly.
    const int c = (std::min)(kDashLength, (std::min)(width, height) / 2);
    add(0, 0, c, t);                          add(0, 0, t, c);
    add(width - c, 0, width, t);              add(width - t, 0, width, c);
    add(0, height - t, c, height);            add(0, height - c, t, height);
    add(width - c, height - t, width, height); add(width - t, height - c, width, height);

    return result;
}

} // namespace

RecordingIndicator& RecordingIndicator::Shared() {
    static RecordingIndicator indicator;
    return indicator;
}

RecordingIndicator::~RecordingIndicator() {
    Hide();
}

// ---------------------------------------------------------------------------

void RecordingIndicator::Show(const RECT& region, std::function<void()> onStop) {
    Hide();

    region_  = region;
    onStop_  = std::move(onStop);
    elapsed_ = L"00:00";
    blinkOn_ = true;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW frame{};
        frame.cbSize        = sizeof(frame);
        frame.lpfnWndProc   = &RecordingIndicator::FrameProc;
        frame.hInstance     = ::GetModuleHandleW(nullptr);
        frame.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        frame.lpszClassName = kFrameClass;

        WNDCLASSEXW pill{};
        pill.cbSize        = sizeof(pill);
        pill.lpfnWndProc   = &RecordingIndicator::PillProc;
        pill.hInstance     = ::GetModuleHandleW(nullptr);
        pill.hCursor       = ::LoadCursorW(nullptr, IDC_HAND);
        pill.lpszClassName = kPillClass;

        // Both atoms are taken before either is judged. Setting `registered`
        // only on full success while leaving a half-registered pair behind
        // would wedge the indicator permanently: the next attempt fails with
        // ERROR_CLASS_ALREADY_EXISTS on the class that did register.
        const ATOM frameAtom = ::RegisterClassExW(&frame);
        const ATOM pillAtom  = ::RegisterClassExW(&pill);
        if (!frameAtom || !pillAtom) {
            logging::Write(L"recorder: couldn't register the indicator windows");
            if (frameAtom) ::UnregisterClassW(kFrameClass, ::GetModuleHandleW(nullptr));
            if (pillAtom)  ::UnregisterClassW(kPillClass, ::GetModuleHandleW(nullptr));
            return;
        }
        registered = true;
    }

    // --- the frame ---
    // Normally the window is the region GROWN by the border thickness, then
    // shaped down to the dashes themselves (see BuildDashRegion). Every pixel
    // it owns is therefore green and sits strictly outside the recorded
    // rectangle: it cannot end up in the video, and it cannot cover what is
    // being recorded. That is a geometric guarantee and needs nothing from the
    // OS.
    //
    // It also cannot work for a full-screen recording, because there is no
    // outside — the grown rectangle falls off the edge of the desktop and the
    // frame is simply never visible. That was the state of things: full-screen
    // recordings had no frame, only the pill in the corner.
    //
    // So: if the region already reaches the edge of its monitor, try to place
    // the frame just INSIDE the region instead, and rely on
    // WDA_EXCLUDEFROMCAPTURE to keep it out of the file. Only if that
    // succeeds. If the affinity call fails — anything before Windows 10
    // 2004 — fall back to the outside placement, which means no visible frame
    // for full screen, exactly as before. A frame burned into every recording
    // is far worse than no frame.
    RECT desktop{};
    MONITORINFO frameMonitor{};
    frameMonitor.cbSize = sizeof(frameMonitor);
    if (::GetMonitorInfoW(::MonitorFromRect(&region_, MONITOR_DEFAULTTONEAREST),
                          &frameMonitor)) {
        desktop = frameMonitor.rcMonitor;
    }

    // ALL four edges, not any of them. "Any" would catch every region merely
    // snapped to a screen edge — dragged to x=0, or the size of a maximised
    // window — and move its frame inside the capture on all four sides, when
    // the geometric placement was still perfectly good on the other three.
    // That is a regression for ordinary region recordings, and the question
    // being asked here is only ever "is this the whole monitor".
    const bool coversMonitor = !::IsRectEmpty(&desktop)
                             && region_.left   <= desktop.left
                             && region_.top    <= desktop.top
                             && region_.right  >= desktop.right
                             && region_.bottom >= desktop.bottom;

    RECT outer = util::InflateRect(region_, kBorderThickness, kBorderThickness);
    bool insideRegion = false;

    frame_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kFrameClass, L"", WS_POPUP,
        outer.left, outer.top, util::RectWidth(outer), util::RectHeight(outer),
        nullptr, nullptr, ::GetModuleHandleW(nullptr), this);

    if (frame_ && coversMonitor) {
        if (ExcludeFromCapture(frame_)) {
            outer        = region_;   // the dashes now sit inside the capture
            insideRegion = true;
            ::SetWindowPos(frame_, nullptr, outer.left, outer.top,
                           util::RectWidth(outer), util::RectHeight(outer),
                           SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            logging::Write(L"recorder: this build of Windows can't hide a window from "
                           L"screen capture, so a full-screen recording has no frame");
        }
    }

    const int outerWidth  = util::RectWidth(outer);
    const int outerHeight = util::RectHeight(outer);

    if (frame_) {
        ScopedRegion dashes = BuildDashRegion(outerWidth, outerHeight, kBorderThickness);
        // SetWindowRgn takes ownership on success, so the handle is released
        // rather than deleted.
        if (dashes && ::SetWindowRgn(frame_, dashes.get(), FALSE)) {
            dashes.release();
            ::ShowWindow(frame_, SW_SHOWNA);
        } else {
            // Unshaped, this window is an opaque slab over the whole
            // recording. No indicator is better than a covered picture.
            logging::Write(L"recorder: couldn't shape the indicator frame, leaving it off");
            ::DestroyWindow(frame_);
            frame_ = nullptr;
        }
    }

    // --- the pill ---
    const RECT pillRect = ChoosePillPosition(region_);
    pill_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kPillClass, L"", WS_POPUP,
        pillRect.left, pillRect.top, util::RectWidth(pillRect), util::RectHeight(pillRect),
        nullptr, nullptr, ::GetModuleHandleW(nullptr), this);
    if (pill_) {
        // Always, not only when the pill has to sit inside the region. It is
        // free when it is not needed, and it fixes a case that was previously
        // just logged and accepted: a region with no room beside it put the
        // Stop button into the recording.
        const bool pillHidden = ExcludeFromCapture(pill_);
        ::ShowWindow(pill_, SW_SHOWNA);

        if (pillInsideCapture_ && !pillHidden) {
            logging::Write(L"recorder: no room beside the region, so the Stop pill sits "
                           L"inside it and will appear in the video");
        }
    }

    // Guarded: the frame can have been destroyed above when it could not be
    // shaped, and logging "frame outside the region" two lines after "leaving
    // it off" is worse than logging nothing.
    if (frame_) {
        logging::Write(util::Format(L"recorder: indicator frame %s the region",
                                    insideRegion ? L"inside" : L"outside"));
    }
}

void RecordingIndicator::Hide() {
    if (pill_)  { ::DestroyWindow(pill_);  pill_  = nullptr; }
    if (frame_) { ::DestroyWindow(frame_); frame_ = nullptr; }
    onStop_ = nullptr;
    pillHot_ = false;
    pillInsideCapture_ = false;
}

void RecordingIndicator::Update(const std::wstring& elapsed, bool blinkOn) {
    if (!pill_) return;
    if (elapsed == elapsed_ && blinkOn == blinkOn_) return;   // nothing moved

    elapsed_ = elapsed;
    blinkOn_ = blinkOn;
    // Only the pill repaints. The frame is static, so a recording costs one
    // small blit a second and nothing else.
    ::InvalidateRect(pill_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------

RECT RecordingIndicator::ChoosePillPosition(const RECT& region) {
    // Measured against the WORST case, not the starting one. The elapsed
    // time is uncapped minutes, so an hour-long recording reads "120:00" and
    // a box sized for "00:00" would clip it. The layout below has to match
    // PaintPill exactly: padding, dot, gap, text, padding.
    int width = 170;
    {
        WindowDC dc(nullptr);
        if (dc) {
            SelectGuard fontGuard(dc.get(), PillFont());
            SIZE dotExtent{}, textExtent{};
            ::GetTextExtentPoint32W(dc.get(), L"●", 1, &dotExtent);
            const wchar_t* longest = L"000:00   Stop";
            ::GetTextExtentPoint32W(dc.get(), longest,
                                    static_cast<int>(::wcslen(longest)), &textExtent);
            width = kPillPadding + dotExtent.cx + kPillDotGap + textExtent.cx + kPillPadding;
        }
    }

    // The work area of the monitor the REGION is on, not the primary one.
    // SystemParametersInfo only ever answers for the primary display, so on a
    // secondary monitor the fit tests below would be measured against the
    // wrong rectangle and the clamps would drag the pill onto the other
    // screen, away from the recording it belongs to.
    RECT work{};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfoW(::MonitorFromRect(&region, MONITOR_DEFAULTTONEAREST), &monitor)) {
        work = monitor.rcWork;
    } else {
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    RECT pill{};
    pill.left  = region.left;
    pill.right = pill.left + width;

    pillInsideCapture_ = false;

    // Below the region if it fits, then above it. Both of those are outside
    // the recorded rectangle, so the pill stays out of the video.
    const int below = region.bottom + kBorderThickness + kPillGap;
    const int above = region.top - kBorderThickness - kPillGap - kPillHeight;
    if (below + kPillHeight <= work.bottom) {
        pill.top = below;
    } else if (above >= work.top) {
        pill.top = above;
    } else {
        // A full-screen recording has no outside. Tucked into the bottom-left
        // of the region, where it is least likely to cover anything, and
        // floored against the region's own top so a short wide region cannot
        // put the pill above itself.
        pill.top   = (std::max)(static_cast<int>(region.top),
                                static_cast<int>(region.bottom) - kPillHeight - kPillGap);
        pill.left  = region.left + kPillGap;
        pill.right = pill.left + width;
        pillInsideCapture_ = true;
    }
    pill.bottom = pill.top + kPillHeight;

    // Keep it on a monitor whatever the region did.
    if (pill.right > work.right) {
        pill.right = work.right - 4;
        pill.left  = pill.right - width;
    }
    if (pill.left < work.left) {
        pill.left  = work.left + 4;
        pill.right = pill.left + width;
    }
    return pill;
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK RecordingIndicator::FrameProc(HWND hwnd, UINT message,
                                               WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<RecordingIndicator*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<RecordingIndicator*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_PAINT:
        self->PaintFrame(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        // Belt and braces with WS_EX_TRANSPARENT: the frame must never take a
        // click meant for whatever is being recorded.
        return HTTRANSPARENT;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void RecordingIndicator::PaintFrame(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(hwnd, &paint);
    if (!dc) { ::EndPaint(hwnd, &paint); return; }

    // The window region is already the dash shape, so every pixel that exists
    // is a dash. Painting is one fill.
    RECT client{};
    ::GetClientRect(hwnd, &client);
    FillSolid(dc, client, kGreen);

    ::EndPaint(hwnd, &paint);
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK RecordingIndicator::PillProc(HWND hwnd, UINT message,
                                              WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<RecordingIndicator*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<RecordingIndicator*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_PAINT:
        self->PaintPill(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE: {
        if (!self->pillHot_) {
            self->pillHot_ = true;
            ::InvalidateRect(hwnd, nullptr, FALSE);
            // Asks for one WM_MOUSELEAVE when the pointer goes, rather than
            // polling.
            TRACKMOUSEEVENT track{};
            track.cbSize    = sizeof(track);
            track.dwFlags   = TME_LEAVE;
            track.hwndTrack = hwnd;
            ::TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        self->pillHot_ = false;
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        // Copied before firing: the callback tears this object's windows
        // down, and running off a member that Hide() has just cleared would
        // be a use-after-free.
        std::function<void()> stop = self->onStop_;
        if (stop) stop();
        return 0;
    }

    case WM_MOUSEACTIVATE:
        // Take the click without stealing focus from whatever is being
        // recorded — a focus change mid-recording is visible in the video.
        return MA_NOACTIVATE;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void RecordingIndicator::PaintPill(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(hwnd, &paint);
    if (!dc) { ::EndPaint(hwnd, &paint); return; }

    RECT client{};
    ::GetClientRect(hwnd, &client);

    FillSolid(dc, client, pillHot_ ? RGB(58, 58, 58) : RGB(28, 28, 28));

    ScopedPen border(::CreatePen(PS_SOLID, 1, pillHot_ ? kRedBright : RGB(90, 90, 90)));
    if (border) {
        SelectGuard penGuard(dc, border.get());
        SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(dc, client.left, client.top, client.right, client.bottom);
    }

    SelectGuard fontGuard(dc, PillFont());
    ::SetBkMode(dc, TRANSPARENT);

    // The dot alternates bright/dim rather than shown/hidden: a dot that
    // disappears reads as "stopped", and removing the glyph would change the
    // text's width and make the whole pill jitter once a second.
    ::SetTextColor(dc, blinkOn_ ? kRedBright : kRedDim);
    RECT dot = client;
    dot.left += kPillPadding;
    ::DrawTextW(dc, L"●", -1, &dot, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SIZE dotExtent{};
    ::GetTextExtentPoint32W(dc, L"●", 1, &dotExtent);

    ::SetTextColor(dc, RGB(240, 240, 240));
    RECT label = client;
    label.left += kPillPadding + dotExtent.cx + kPillDotGap;
    label.right -= kPillPadding;
    const std::wstring text = elapsed_ + L"   Stop";
    ::DrawTextW(dc, text.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ::EndPaint(hwnd, &paint);
}
