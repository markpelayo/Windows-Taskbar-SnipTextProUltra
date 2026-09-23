// Toast.h — the brief confirmation that replaces the macOS menubar title.
//
// The macOS original shows a character count beside its menubar icon and
// clears it a moment later. Windows has no such surface, and a notification
// balloon is both heavyweight and a thing the user can be asked to grant
// permission for. So this is a small borderless window above the taskbar,
// shown for 1.6 seconds and then gone.
//
// One window is created lazily and reused for the life of the process; it
// costs nothing while hidden.

#pragma once

#include "framework.h"

namespace toast {

// Replaces whatever is showing. Any pending dismissal is cancelled first, so
// rapid captures do not clear an in-flight message early.
void Show(const std::wstring& message);

// Suppressed while recording, because the recording indicator owns that
// surface and would overwrite the message a second later.
void SetSuppressed(bool suppressed);

// True while a message is on screen. The recording indicator checks this so
// it does not stomp a live "saved" confirmation.
bool IsShowing();

void Hide();
void Destroy();

} // namespace toast
