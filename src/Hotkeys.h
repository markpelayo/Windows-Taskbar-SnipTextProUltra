// Hotkeys.h — the six global shortcuts, and the ability to rebind them.
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
};

constexpr int kActionCount = 6;
extern const Action kAllActions[kActionCount];

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
