#include "Hotkeys.h"

// For ToolTitle: the eight tool shortcuts take their menu names from the
// tool model rather than repeating them here. See ActionTitle.
#include "Annotation.h"
#include "Settings.h"
#include "Util.h"

namespace hotkeys {
// Menu order, and the menu splits itself on IsGlobal — so the six globals
// must stay first and the editor-only ones after them, with CloseEditor
// leading that group because Esc is the one everybody already knows.
const Action kAllActions[kActionCount] = {
    Action::ScreenshotRegion, Action::ScreenshotFullScreen,
    Action::TextRegion,       Action::TextFullScreen,
    Action::RecordRegion,     Action::RecordFullScreen,
    Action::CloseEditor,
    Action::SelectTool1, Action::SelectTool2, Action::SelectTool3,
    Action::SelectTool4, Action::SelectTool5, Action::SelectTool6,
    Action::SelectTool7, Action::SelectTool8,
    Action::TogglePin,
};

namespace {
constexpr const wchar_t* kWindowClass = L"SnipTextProUltraHotkeyCapture";

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
            // The footer has to tell the truth about Esc, which means one
            // of two sentences depending on whether Esc is a legal binding
            // for the action being changed.
            // Same condition as the Esc handler below, for the same
            // reason — and they have to stay the same condition, because
            // this line is the only warning a user gets.
            const wchar_t* footerText =
                state->action != Action::CloseEditor
                    ? L"Enter to save  ·  Delete to unbind  ·  Esc to cancel"
                    : L"Enter to save  ·  Delete to unbind  ·  click away to cancel";
            ::DrawTextW(dc, footerText,
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

        // Esc backs out — except for CloseEditor, the one action whose
        // whole point is that Esc can be bound to it. There the key has to
        // be capturable, or unbinding Close the Screenshot Editor once
        // would make its own default unreachable forever without resetting
        // every other shortcut too. Deactivating the window still cancels,
        // so there is always a way out.
        //
        // Tested against THAT ACTION, not against IsGlobal. This was
        // `IsGlobal(state->action)` and it was correct only while
        // CloseEditor was the sole non-global action. When 1.9.5 added nine
        // more, the exception silently widened to all ten — and the
        // consequence was not merely "Esc does not cancel here": Esc would
        // be recorded as the candidate binding, Enter would commit it, and
        // the de-confliction pass in App would then find CloseEditor
        // holding Esc IN THE SAME SCOPE and quietly unbind it. Pressing Esc
        // to back out of the Rectangle row would have destroyed
        // Esc-to-close, permanently and without a word.
        if (key == VK_ESCAPE && state->action != Action::CloseEditor) {
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
        // The bare-key rule applies to GLOBAL bindings only. It exists
        // because registering an unmodified key takes it from every
        // program on the machine; a local action registers nothing, so
        // the reason does not apply — and Esc, its own default, would
        // otherwise be impossible to bind back after one unbind.
        if (modifiers == 0 && IsGlobal(state->action) &&
            !(key >= VK_F1 && key <= VK_F24)) {
            ::MessageBeep(MB_ICONWARNING);
            return 0;
        }

        // The editor's HARD-CODED keys are off limits, and this is the one
        // rejection in this file that is about something outside it.
        //
        // Ctrl+Z/Y/C/S, Ctrl(+Shift)+Up/Down, and bare Delete, Backspace,
        // F2 and the arrows are all handled directly by the editor's key
        // handler. They are not hotkeys::Actions at all, so App's
        // de-confliction pass — which only compares the sixteen rebindable
        // actions against each other — cannot see them. Bind a tool to
        // Ctrl+Z and the collision is silent and unrecoverable: undo simply
        // stops working, nothing is said at bind time, and the only way
        // back is Reset to Defaults.
        //
        // Refused rather than de-conflicted, because the other side of the
        // collision is not a binding that can be moved out of the way.
        //
        // APPLIED TO GLOBAL ACTIONS TOO, which is the opposite of the first
        // version of this check. The reasoning that globals do not matter
        // here — "the global fires first, so who cares" — is backwards:
        // RegisterHotKey claims the combination SYSTEM-WIDE and the
        // keystroke is then never dispatched to anybody, so a global on
        // Ctrl+Z kills editor undo by the same mechanism AND takes Ctrl+Z
        // from every other program on the machine. Strictly the worse case.
        {
            const bool ctrl      = (modifiers & MOD_CONTROL) != 0;
            const bool onlyCtrl  = (modifiers & ~(MOD_CONTROL | MOD_SHIFT)) == 0;
            const bool editorCtrl = ctrl && onlyCtrl &&
                (key == 'Z' || key == 'Y' || key == 'C' || key == 'S' ||
                 key == VK_UP || key == VK_DOWN);

            // Ctrl+Shift+Z is Redo, so testing for Ctrl alone was not
            // enough; Shift is allowed through above and then ignored.
            //
            // The bare keys are a local-only concern: the rule above
            // already refuses any unmodified non-function key for a global.
            const bool editorBare = modifiers == 0 && !IsGlobal(state->action) &&
                (key == VK_DELETE || key == VK_BACK || key == VK_F2 ||
                 key == VK_LEFT || key == VK_RIGHT ||
                 key == VK_UP   || key == VK_DOWN);

            if (editorCtrl || editorBare) {
                ::MessageBeep(MB_ICONWARNING);
                return 0;
            }
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
    // These have to read the same as the commands they bind, or the Change
    // Keyboard Shortcut submenu becomes a second set of names for the same
    // things.
    case Action::ScreenshotRegion:     return L"Screenshot a Region";
    case Action::ScreenshotFullScreen: return L"Screenshot Full Screen";
    case Action::TextRegion:           return L"ScreenshotToText a Region";
    case Action::TextFullScreen:       return L"ScreenshotToText Full Screen";
    case Action::RecordRegion:         return L"Screen Record a Region";
    case Action::RecordFullScreen:     return L"Screen Record Full Screen";
    case Action::CloseEditor:          return L"Close the Screenshot Editor";

    // Not literals. The eight tool rows take their names from ToolTitle,
    // the same function the tooltips use, so the menu cannot end up calling
    // a tool something the tooltip does not — and reordering the toolbar
    // reorders these rows with it, automatically and correctly.
    case Action::SelectTool1: case Action::SelectTool2:
    case Action::SelectTool3: case Action::SelectTool4:
    case Action::SelectTool5: case Action::SelectTool6:
    case Action::SelectTool7: case Action::SelectTool8:
        return ToolTitle(static_cast<Tool>(ToolActionIndex(action)));

    // Word for word the tray row it mirrors. Two names for one switch is
    // how the user ends up believing there are two switches.
    case Action::TogglePin:            return L"Keep the Editor on Top";
    }
    return L"";
}

bool IsToolAction(Action action) {
    return action >= Action::SelectTool1 && action <= Action::SelectTool8;
}

int ToolActionIndex(Action action) {
    if (!IsToolAction(action)) return -1;
    return static_cast<int>(action) - static_cast<int>(Action::SelectTool1);
}

bool IsGlobal(Action action) {
    // Everything from CloseEditor onwards is matched by the editor itself.
    //
    // Stated as a threshold rather than as a list of exceptions on purpose.
    // The list version was `action != CloseEditor`, which would have
    // silently registered nine bare number keys as SYSTEM-WIDE hotkeys the
    // moment they were added — taking 1 through 9 away from every other
    // program on the machine. A threshold cannot be forgotten when the next
    // editor-only action is appended.
    return static_cast<int>(action) < static_cast<int>(Action::CloseEditor);
}

UINT CurrentModifiers() {
    UINT modifiers = 0;
    if (::GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (::GetKeyState(VK_SHIFT)   & 0x8000) modifiers |= MOD_SHIFT;
    if (::GetKeyState(VK_MENU)    & 0x8000) modifiers |= MOD_ALT;
    // GetAsyncKeyState for the Windows keys, matching what the capture
    // window uses. GetKeyState reports the state as of the message being
    // processed, and the shell swallows most Win combinations before that
    // message exists — so the two would disagree and a captured Win binding
    // would never match.
    if ((::GetAsyncKeyState(VK_LWIN) & 0x8000) || (::GetAsyncKeyState(VK_RWIN) & 0x8000)) {
        modifiers |= MOD_WIN;
    }
    return modifiers;
}

bool Matches(Action action, UINT key, UINT modifiers) {
    const Binding binding = Current(action);
    // An unbound action matches nothing — that is what unbinding means, and
    // without this check every keystroke with no modifiers would match a
    // binding whose key is 0.
    if (!binding.IsBound()) return false;
    return binding.key == key && binding.modifiers == modifiers;
}

Binding Default(Action action) {
    // Esc, alone. The editor is a window you dismiss rather than a command
    // you invoke, and a bare key is safe here precisely because this
    // binding is not global — a system-wide Esc would be a catastrophe.
    if (action == Action::CloseEditor) {
        Binding binding;
        binding.key = VK_ESCAPE;
        return binding;
    }

    // Bare 1 through 8 for the tools, 9 for Pin, matching the buttons left
    // to right. Bare digits are only defensible because these are editor-
    // local: the editor's own key handler stands down entirely while a text
    // label is being typed, so typing "3 items" into a label does not
    // switch tools.
    if (IsToolAction(action)) {
        Binding binding;
        binding.key = static_cast<UINT>('1' + ToolActionIndex(action));
        return binding;
    }
    if (action == Action::TogglePin) {
        Binding binding;
        binding.key = '9';
        return binding;
    }

    // Ctrl+Shift+1 through 6 for the six globals, in menu order. Only
    // reached now that every non-global action has returned above, and the
    // derivation from the enum value is why those had to be APPENDED.
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
    case VK_ESCAPE: return out + L"Esc";
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
        // Editor-only actions are never handed to RegisterHotKey. Doing so
        // would take the key away from every other program on the machine,
        // and these default to a bare Esc and the bare digits 1 through 9 —
        // which system-wide would make the computer close to unusable.
        if (!IsGlobal(action)) continue;
        const Binding binding = Current(action);
        if (!binding.IsBound()) continue;

        // MOD_NOREPEAT: a held key should fire once, not open a crosshair per
        // repeat tick.
        // The return value is deliberately ignored. A shortcut another
        // program already owns cannot be registered, and there is nothing
        // useful to do about it at startup: the action stays reachable from
        // the menu, and Change Keyboard Shortcut is where it gets fixed. A
        // dialog here would fire before the user has any context for it.
        ::RegisterHotKey(owner, static_cast<int>(action),
                         binding.modifiers | MOD_NOREPEAT, binding.key);
    }
}

void Unregister(HWND owner) {
    if (!owner) return;
    for (Action action : kAllActions) {
        if (!IsGlobal(action)) continue;   // never registered; see Register
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
