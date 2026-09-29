// Hotkeys.h — the seven rebindable shortcuts. Six are global; the last is
// matched inside the editor. See IsGlobal below for why that distinction
// is not a detail.
//
// A binding is a modifier mask plus a virtual-key code. Both halves are
// optional in the sense that matters: a binding with no modifiers is allowed,
// so F9 on its own works, and a binding with no key at all means "not bound",
// which is how a shortcut is switched off.
//
// Bindings live in the registry beside everything else. A value that has
// never been written reads as its default, so "reset to defaults" removes the
// values rather than writing them back — the same rule the rest of the
// settings follow, and what keeps Sanitize's is-it-default test honest.

#pragma once

#include "framework.h"

namespace hotkeys {

// Numbered in menu order, top to bottom, so the menu itself is the reminder.
// The values are also the RegisterHotKey ids.
enum class Action {
    ScreenshotRegion = 1,
    ScreenshotFullScreen,
    TextRegion,
    TextFullScreen,
    RecordRegion,
    RecordFullScreen,
    // Editor-only, and the first of its kind. Everything above is a GLOBAL
    // hotkey: registered with RegisterHotKey, fires wherever you are, and
    // is therefore taken away from every other program. This one must not
    // be — its default is Esc on its own, and a system-wide Esc would be a
    // catastrophe. It is checked by the editor's own key handler instead.
    CloseEditor,
};

constexpr int kActionCount = 7;
extern const Action kAllActions[kActionCount];

// Whether an action is registered system-wide. False means the window that
// cares about it looks for the key itself, so the binding is rebindable and
// unbindable in exactly the same way without ever leaving this process.
bool IsGlobal(Action action);

// Does this keystroke match the action's current binding? `modifiers` is a
// MOD_* mask, which the caller assembles from GetKeyState — VK codes and
// MOD_ flags are different vocabularies and mixing them silently matches
// nothing.
bool Matches(Action action, UINT key, UINT modifiers);

// The MOD_* mask for the modifier keys held down right now.
UINT CurrentModifiers();

struct Binding {
    UINT modifiers = 0;   // MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN
    UINT key       = 0;   // a virtual-key code; 0 means "not bound"

    bool IsBound() const { return key != 0; }
    bool operator==(const Binding& other) const {
        return modifiers == other.modifiers && key == other.key;
    }
};

const wchar_t* ActionTitle(Action action);   // "Screenshot Region"

Binding Current(Action action);
Binding Default(Action action);
void    Set(Action action, const Binding& binding);
void    ResetAll();
bool    IsDefault();

// "Ctrl+Shift+1", "F9", or "Not set".
std::wstring Describe(const Binding& binding);

// Registers every bound shortcut against `owner`. Any that fails — because
// another program already owns it — is logged and skipped; its action is
// simply not reachable by keyboard for the session, and the menu item still
// works. Call Unregister first if anything may already be registered.
void Register(HWND owner);
void Unregister(HWND owner);

// Opens a small modal window that waits for a key combination and returns it.
//
// It unregisters our own hotkeys for its lifetime, because a global hotkey
// fires before the foreground window sees the key — so without that, pressing
// the shortcut you are trying to change would trigger it instead of being
// captured.
//
// Returns false if the user cancelled. Delete or Backspace returns an unbound
// Binding, which is how a shortcut is switched off.
bool CaptureBinding(HWND owner, Action action, Binding* result);

} // namespace hotkeys
