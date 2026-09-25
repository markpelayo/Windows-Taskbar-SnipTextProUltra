#include "Hotkeys.h"

#include "Log.h"
#include "Settings.h"
#include "Util.h"

namespace hotkeys {

const Action kAllActions[kActionCount] = {
    Action::ScreenshotRegion, Action::ScreenshotFullScreen,
    Action::TextRegion,       Action::TextFullScreen,
    Action::RecordRegion,     Action::RecordFullScreen,
};

namespace {

constexpr const wchar_t* kWindowClass = L"SnipTextHotkeyCapture";

// One registry value per action, named by index so the set is obvious when
// you look at the key in regedit.
std::wstring SettingsName(Action action) {
    return util::Format(L"hotkey%d", static_cast<int>(action));
}

// Packed as (modifiers << 16) | key. A single DWORD keeps the two halves
// impossible to get out of step with each other.
int Pack(const Binding& binding) {
    return static_cast<int>((binding.modifiers << 16) | (binding.key & 0xFFFF));
}

Binding Unpack(int stored) {
    Binding binding;
    binding.modifiers = static_cast<UINT>((stored >> 16) & 0xFFFF);
    binding.key       = static_cast<UINT>(stored & 0xFFFF);
    return binding;
}

bool IsModifierKey(UINT key) {
    switch (key) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:
    case VK_MENU:    case VK_LMENU:    case VK_RMENU:
    case VK_LWIN:    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

// --- the capture window ---

struct CaptureState {
    Binding  binding;
    bool     captured  = false;   // a key was actually pressed in here
    bool     confirmed = false;
    bool     finished  = false;
    Action   action    = Action::ScreenshotRegion;
};

// Ends the modal loop. The flag alone is not enough: WM_ACTIVATE and WM_CLOSE
// are SENT rather than posted, so the loop would still be blocked inside
// GetMessage with nothing to wake it — leaving an invisible window up and the
// app's hotkeys unregistered. The posted WM_NULL is what wakes it.
void Finish(HWND hwnd, CaptureState* state, bool confirmed) {
    state->confirmed = confirmed;
    state->finished  = true;
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
}

HFONT CaptureFont(int height, int weight) {
    LOGFONTW description{};
    description.lfHeight  = -height;
    description.lfWeight  = weight;
    description.lfQuality = CLEARTYPE_QUALITY;
    ::wcscpy_s(description.lfFaceName, L"Segoe UI");
    return ::CreateFontIndirectW(&description);
}

LRESULT CALLBACK CaptureProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CaptureState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<CaptureState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return ::DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(hwnd, &paint);
        if (!dc) { ::EndPaint(hwnd, &paint); return 0; }

        RECT client{};
        ::GetClientRect(hwnd, &client);

        ScopedBrush background(::CreateSolidBrush(RGB(32, 32, 32)));
        if (background) ::FillRect(dc, &client, background.get());
        ScopedPen border(::CreatePen(PS_SOLID, 1, RGB(0, 120, 212)));
        if (border) {
            SelectGuard penGuard(dc, border.get());
            SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));
            ::Rectangle(dc, client.left, client.top, client.right, client.bottom);
        }

        ::SetBkMode(dc, TRANSPARENT);

        ScopedFont title(CaptureFont(16, FW_SEMIBOLD));
        if (title) {
            SelectGuard fontGuard(dc, title.get());
            ::SetTextColor(dc, RGB(170, 170, 170));
            RECT row = client;
            row.top += 18;
            const std::wstring heading =
                std::wstring(L"New shortcut for ") + ActionTitle(state->action);
            ::DrawTextW(dc, heading.c_str(), -1, &row,
                        DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        ScopedFont big(CaptureFont(30, FW_SEMIBOLD));
        if (big) {
            SelectGuard fontGuard(dc, big.get());
            ::SetTextColor(dc, RGB(245, 245, 245));
            RECT row = client;
            row.top += 52;
            const std::wstring shown = state->binding.IsBound()
                                     ? Describe(state->binding)
                                     : std::wstring(L"Press a key…");
            ::DrawTextW(dc, shown.c_str(), -1, &row,
                        DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Not named `small`: rpcndr.h does `#define small char` and arrives
        // transitively through objbase.h, so `ScopedFont small(...)` expands
        // to `ScopedFont char(...)`.
        ScopedFont footer(CaptureFont(14, FW_NORMAL));
        if (footer) {
            SelectGuard fontGuard(dc, footer.get());
            ::SetTextColor(dc, RGB(150, 150, 150));
            RECT row = client;
            row.bottom -= 16;
            ::DrawTextW(dc,
                        L"Enter to save  ·  Delete to unbind  ·  Esc to cancel",
                        -1, &row, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
        }

        ::EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_GETDLGCODE:
        // Claim everything, or the dialog manager eats Enter, Esc and the
        // arrow keys before this window ever sees them.
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const UINT key = static_cast<UINT>(wParam);

        if (key == VK_ESCAPE) {
            Finish(hwnd, state, false);
            return 0;
        }
        if (key == VK_RETURN) {
            // Only commits a combination that was actually pressed in here.
            // Enter on an untouched window is a no-op, not a re-commit of
            // whatever was already bound.
            Finish(hwnd, state, state->captured);
            return 0;
        }
        if (key == VK_DELETE || key == VK_BACK) {
            state->binding = Binding{};        // deliberately unbound
            Finish(hwnd, state, true);
            return 0;
        }
        // A modifier on its own is not a shortcut; wait for the real key.
        if (IsModifierKey(key)) return 0;

        UINT modifiers = 0;
        if (::GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
        if (::GetKeyState(VK_SHIFT)   & 0x8000) modifiers |= MOD_SHIFT;
        if (::GetKeyState(VK_MENU)    & 0x8000) modifiers |= MOD_ALT;
        if ((::GetAsyncKeyState(VK_LWIN) & 0x8000) ||
            (::GetAsyncKeyState(VK_RWIN) & 0x8000)) {
            modifiers |= MOD_WIN;
        }

        // A bare key with no modifiers is only allowed for the function keys.
        // Binding a plain letter would register it globally and swallow it in
        // every program on the machine — including this window, so the user
        // could not type the combination needed to undo it.
        if (modifiers == 0 && !(key >= VK_F1 && key <= VK_F24)) {
            ::MessageBeep(MB_ICONWARNING);
            return 0;
        }

        state->binding.modifiers = modifiers;
        state->binding.key       = key;
        state->captured          = true;
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_ACTIVATE:
        // Clicking away is a cancel. Leaving an invisible modal window
        // holding the hotkeys unregistered would be much worse.
        if (LOWORD(wParam) == WA_INACTIVE && !state->finished) {
            Finish(hwnd, state, false);
        }
        return 0;

    case WM_CLOSE:
        Finish(hwnd, state, false);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

// ---------------------------------------------------------------------------

const wchar_t* ActionTitle(Action action) {
    switch (action) {
    // These have to read the same as the commands they bind, or the Shortcuts
    // submenu becomes a second set of names for the same six things.
    case Action::ScreenshotRegion:     return L"Screenshot a Region";
    case Action::ScreenshotFullScreen: return L"Screenshot Full Screen";
    case Action::TextRegion:           return L"ScreenshotToText a Region";
    case Action::TextFullScreen:       return L"ScreenshotToText Full Screen";
    case Action::RecordRegion:         return L"Record Region";
    case Action::RecordFullScreen:     return L"Record Full Screen";
    }
    return L"";
}

Binding Default(Action action) {
    // Ctrl+Shift+1 through 6, in menu order.
    Binding binding;
    binding.modifiers = MOD_CONTROL | MOD_SHIFT;
    binding.key       = static_cast<UINT>('0' + static_cast<int>(action));
    return binding;
}

Binding Current(Action action) {
    const std::wstring name = SettingsName(action);
    // A value that was never written reads as its default, so "not stored"
    // and "at the default" are the same thing.
    if (!settings::Exists(name.c_str())) return Default(action);

    const Binding stored = Unpack(settings::GetInt(name.c_str(), 0));
    // A stored key of 0 is a deliberate "unbound", not a missing value —
    // the Exists check above already separated those two.
    return stored;
}

void Set(Action action, const Binding& binding) {
    const std::wstring name = SettingsName(action);
    if (binding == Default(action)) {
        settings::Remove(name.c_str());   // back to being absent
    } else {
        settings::SetInt(name.c_str(), Pack(binding));
    }
}

void ResetAll() {
    for (Action action : kAllActions) {
        settings::Remove(SettingsName(action).c_str());
    }
}

bool IsDefault() {
    for (Action action : kAllActions) {
        if (!(Current(action) == Default(action))) return false;
    }
    return true;
}

std::wstring Describe(const Binding& binding) {
    if (!binding.IsBound()) return L"Not set";

    std::wstring out;
    if (binding.modifiers & MOD_CONTROL) out += L"Ctrl+";
    if (binding.modifiers & MOD_ALT)     out += L"Alt+";
    if (binding.modifiers & MOD_SHIFT)   out += L"Shift+";
    if (binding.modifiers & MOD_WIN)     out += L"Win+";

    // Named keys first, because GetKeyNameText gives some of them names that
    // are localised or simply unhelpful.
    switch (binding.key) {
    case VK_SPACE:  return out + L"Space";
    case VK_RETURN: return out + L"Enter";
    case VK_TAB:    return out + L"Tab";
    case VK_INSERT: return out + L"Insert";
    case VK_HOME:   return out + L"Home";
    case VK_END:    return out + L"End";
    case VK_PRIOR:  return out + L"Page Up";
    case VK_NEXT:   return out + L"Page Down";
    case VK_SNAPSHOT: return out + L"Print Screen";
    case VK_PAUSE:  return out + L"Pause";
    case VK_LEFT:   return out + L"Left";
    case VK_RIGHT:  return out + L"Right";
    case VK_UP:     return out + L"Up";
    case VK_DOWN:   return out + L"Down";
    default: break;
    }
    if (binding.key >= VK_F1 && binding.key <= VK_F24) {
        return out + util::Format(L"F%u", binding.key - VK_F1 + 1);
    }
    if ((binding.key >= '0' && binding.key <= '9') ||
        (binding.key >= 'A' && binding.key <= 'Z')) {
        return out + static_cast<wchar_t>(binding.key);
    }
    if (binding.key >= VK_NUMPAD0 && binding.key <= VK_NUMPAD9) {
        return out + util::Format(L"Numpad %u", binding.key - VK_NUMPAD0);
    }

    // Anything else: ask the keyboard layout, which at least gets the
    // punctuation keys right for the user's actual keyboard.
    const UINT scan = ::MapVirtualKeyW(binding.key, MAPVK_VK_TO_VSC_EX);
    if (scan != 0) {
        LONG parameter = static_cast<LONG>((scan & 0xFF) << 16);
        // Bit 24 marks an extended key. Without it the layout answers with
        // the non-extended twin's name — numpad Divide comes back as Slash.
        if (scan & 0xFF00) parameter |= (1L << 24);
        wchar_t name[64]{};
        if (::GetKeyNameTextW(parameter, name, 64) > 0) return out + name;
    }
    return out + util::Format(L"Key %u", binding.key);
}

// ---------------------------------------------------------------------------

void Register(HWND owner) {
    if (!owner) return;
    for (Action action : kAllActions) {
        const Binding binding = Current(action);
        if (!binding.IsBound()) continue;

        // MOD_NOREPEAT: a held key should fire once, not open a crosshair per
        // repeat tick.
        if (!::RegisterHotKey(owner, static_cast<int>(action),
                              binding.modifiers | MOD_NOREPEAT, binding.key)) {
            // The action is deliberately still reachable from the menu. A
            // dialog at startup about a shortcut the user can neither see nor
            // fix would be worse than a log line.
            logging::Write(util::Format(L"hotkey: %s is already taken by another program, "
                                        L"%s is menu-only this session",
                                        Describe(binding).c_str(),
                                        ActionTitle(action)));
        }
    }
}

void Unregister(HWND owner) {
    if (!owner) return;
    for (Action action : kAllActions) {
        ::UnregisterHotKey(owner, static_cast<int>(action));
    }
}

// ---------------------------------------------------------------------------

bool CaptureBinding(HWND owner, Action action, Binding* result) {
    if (!result) return false;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW description{};
        description.cbSize        = sizeof(description);
        description.lpfnWndProc   = &CaptureProc;
        description.hInstance     = ::GetModuleHandleW(nullptr);
        description.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        description.lpszClassName = kWindowClass;
        if (!::RegisterClassExW(&description)) {
            logging::Write(L"hotkey: couldn't create the capture window");
            return false;
        }
        registered = true;
    }

    // Our own shortcuts have to stand down for the duration. A global hotkey
    // fires before the foreground window sees the key, so without this the
    // shortcut you are trying to change would trigger its action instead of
    // being captured.
    Unregister(owner);

    CaptureState state;
    state.action  = action;
    state.binding = Current(action);

    constexpr int width  = 420;
    constexpr int height = 170;
    // Seeded with the primary display, so a window still lands somewhere
    // visible if both lookups below fail rather than being centred on a
    // zeroed rectangle at negative coordinates.
    RECT work{ 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfoW(::MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        work = monitor.rcWork;
    } else {
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    HWND hwnd = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kWindowClass, L"", WS_POPUP,
        work.left + (util::RectWidth(work) - width) / 2,
        work.top + (util::RectHeight(work) - height) / 2,
        width, height, owner, nullptr, ::GetModuleHandleW(nullptr), &state);

    if (!hwnd) {
        Register(owner);
        return false;
    }

    ::ShowWindow(hwnd, SW_SHOW);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(hwnd);

    // A modal loop of its own, the same shape the region overlay uses: the
    // caller reads as straight-line code and there is no half-finished
    // rebinding to keep track of anywhere.
    MSG message{};
    while (!state.finished) {
        const BOOL got = ::GetMessageW(&message, nullptr, 0, 0);
        if (got == 0) {
            // WM_QUIT arrived. Put it back — the app's own loop still needs
            // to see it — and treat this as a cancel.
            ::PostQuitMessage(static_cast<int>(message.wParam));
            state.confirmed = false;
            break;
        }
        if (got == -1) break;
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    ::DestroyWindow(hwnd);
    Register(owner);   // whatever happened, the shortcuts come back

    if (!state.confirmed) return false;
    *result = state.binding;
    return true;
}

} // namespace hotkeys
