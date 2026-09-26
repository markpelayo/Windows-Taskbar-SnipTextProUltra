#include "Toast.h"

#include "Util.h"

namespace toast {
namespace {

constexpr const wchar_t* kWindowClass = L"SnipTextToast";
constexpr UINT_PTR kDismissTimer = 1;
// Three seconds. 1.6 was long enough to notice the message and not long
// enough to read it — the difference matters because these are the only
// confirmation the program gives, there being no notification banner anywhere
// in it. The longest of them is now a full sentence, and a sentence needs
// longer than a glance.
constexpr UINT     kDurationMs   = 3000;
constexpr int      kPaddingX     = 18;
constexpr int      kPaddingY     = 10;
constexpr int      kMargin       = 16;

HWND         g_window     = nullptr;
std::wstring g_message;
bool         g_suppressed = false;
bool         g_showing    = false;
// Set in Show, read in the paint handler: whether the message had to be cut
// to fit the work area, which decides between centring it and ellipsising it.
bool         g_truncates  = false;

HFONT ToastFont() {
    static HFONT font = nullptr;
    if (!font) {
        LOGFONTW description{};
        description.lfHeight  = -18;
        description.lfWeight  = FW_SEMIBOLD;
        description.lfQuality = CLEARTYPE_QUALITY;
        ::wcscpy_s(description.lfFaceName, L"Segoe UI");
        font = ::CreateFontIndirectW(&description);
    }
    return font;
}

LRESULT CALLBACK ToastProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(hwnd, &paint);
        if (!dc) return 0;

        RECT client{};
        ::GetClientRect(hwnd, &client);

        ScopedBrush background(::CreateSolidBrush(RGB(32, 32, 32)));
        if (background) ::FillRect(dc, &client, background.get());

        ScopedPen border(::CreatePen(PS_SOLID, 1, RGB(90, 90, 90)));
        if (border) {
            SelectGuard penGuard(dc, border.get());
            SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));
            ::Rectangle(dc, client.left, client.top, client.right, client.bottom);
        }

        SelectGuard fontGuard(dc, ToastFont());
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, RGB(245, 245, 245));
        // Centred when it fits, because that is what a short confirmation
        // should look like. Left-aligned and ellipsised when it does not,
        // because DT_CENTER with DT_END_ELLIPSIS trims both ends and would
        // drop the first words as well as the last.
        RECT text = client;
        UINT flags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        if (g_truncates) {
            text.left  += kPaddingX;
            text.right -= kPaddingX;
            flags |= DT_LEFT | DT_END_ELLIPSIS;
        } else {
            flags |= DT_CENTER;
        }
        ::DrawTextW(dc, g_message.c_str(), -1, &text, flags);

        ::EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_TIMER:
        if (wParam == kDismissTimer) {
            ::KillTimer(hwnd, kDismissTimer);
            Hide();
        }
        return 0;
    case WM_LBUTTONUP:
        Hide();
        return 0;
    case WM_NCHITTEST:
        // The whole window is client area, so a click lands on WM_LBUTTONUP
        // above and dismisses the message early. It does take that one click
        // — which is the trade for being able to get rid of it deliberately
        // rather than waiting out the timer.
        return HTCLIENT;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND EnsureWindow() {
    if (g_window) return g_window;

    WNDCLASSEXW description{};
    description.cbSize        = sizeof(description);
    description.lpfnWndProc   = &ToastProc;
    description.hInstance     = ::GetModuleHandleW(nullptr);
    description.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    description.lpszClassName = kWindowClass;
    ::RegisterClassExW(&description);

    // NOACTIVATE so showing a message never pulls focus away from whatever
    // the user was typing into.
    g_window = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWindowClass, L"", WS_POPUP, 0, 0, 10, 10,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    return g_window;
}

} // namespace

void SetSuppressed(bool suppressed) {
    g_suppressed = suppressed;
    if (suppressed) Hide();
}

bool IsShowing() { return g_showing; }

void Show(const std::wstring& message) {
    if (g_suppressed || message.empty()) return;

    HWND window = EnsureWindow();
    if (!window) return;

    g_message = message;

    // Measure, so the box is exactly as wide as the text it carries.
    SIZE extent{ 120, 24 };
    {
        WindowDC dc(window);
        if (dc) {
            SelectGuard fontGuard(dc.get(), ToastFont());
            ::GetTextExtentPoint32W(dc.get(), message.c_str(),
                                    static_cast<int>(message.size()), &extent);
        }
    }
    const int height = extent.cy + kPaddingY * 2;

    // Bottom-right of the work area, which on a default Windows 11 setup puts
    // it just above the taskbar and clear of the notification area.
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // The WIDTH is what has to be clamped, not the position. Clamping only x
    // leaves an over-wide box overhanging the right edge instead of the left,
    // which is the same amount of lost text in the other direction. Messages
    // are full sentences now and the longest carries a generated file name —
    // "Recording saved · SnipText 2026-09-26 at 09.49.23.984.mp4" — so on a
    // narrow display this is reachable rather than theoretical.
    const int maxWidth = (std::max)(120, util::RectWidth(work) - kMargin * 2);
    const int width    = (std::min)(maxWidth, static_cast<int>(extent.cx) + kPaddingX * 2);
    // Clamped to the work area. The messages are full sentences now, and the
    // longest of them carries a file name of whatever length the clock
    // produced — "Recording saved · SnipText 2026-09-26 at 09.49.23.984.mp4".
    // Unclamped, a long one runs off the left edge of the screen and the
    // beginning of the message, which is the part that says what happened, is
    // the part that disappears.
    const int x = (std::max)(static_cast<int>(work.left) + kMargin,
                             static_cast<int>(work.right) - width - kMargin);
    const int y = work.bottom - height - kMargin;

    // Truncation, when it happens, has to take the END. These messages lead
    // with what happened and trail with the detail, so losing the tail costs
    // a file name and losing the head costs the meaning.
    g_truncates = width < static_cast<int>(extent.cx) + kPaddingX * 2;

    // Any pending dismissal is cancelled first, so a second capture does not
    // clear the first message early.
    ::KillTimer(window, kDismissTimer);
    ::SetWindowPos(window, HWND_TOPMOST, x, y, width, height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::InvalidateRect(window, nullptr, TRUE);
    ::SetTimer(window, kDismissTimer, kDurationMs, nullptr);
    g_showing = true;
}

void Hide() {
    g_showing = false;
    if (!g_window) return;
    ::KillTimer(g_window, kDismissTimer);
    ::ShowWindow(g_window, SW_HIDE);
}

void Destroy() {
    if (!g_window) return;
    ::DestroyWindow(g_window);
    g_window  = nullptr;
    g_showing = false;
}

} // namespace toast
