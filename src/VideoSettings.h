// VideoSettings.h — frame rate, quality, cursor, clicks, audio input.
//
// Video Settings lives in the flyout menu rather than behind a gear button,
// so every setting in the app is in one place.

#pragma once

#include "framework.h"

namespace video {

enum class Quality { High, Medium, Low };

struct Microphone {
    std::wstring id;     // the WASAPI endpoint ID
    std::wstring name;   // the friendly name shown in the menu
};

// --- frame rate ---
extern const int kFrameRateChoices[4];   // 15, 24, 30, 60
constexpr int kDefaultFrameRate = 30;

int  FrameRate();
void SetFrameRate(int fps);

// --- quality ---
// Applied as an output scale, not as a bitrate. A scale factor is one number
// that cannot be subtly wrong; a hand-built encoder configuration can be, and
// fails at runtime rather than at build time. The menu labels say what it
// actually does.
Quality      CurrentQuality();
void         SetQuality(Quality quality);
double       QualityScale(Quality quality);
double       CurrentQualityScale();
const wchar_t* QualityTitle(Quality quality);       // "High — full resolution"
const wchar_t* QualityShortTitle(Quality quality);  // "High"

// --- cursor and clicks ---
bool CapturesCursor();
void SetCapturesCursor(bool value);
bool CapturesClicks();
void SetCapturesClicks(bool value);

// --- audio ---
// An empty ID means "do not record audio", which is the shipped default.
std::wstring AudioDeviceId();
void         SetAudioDeviceId(const std::wstring& id);

// Enumerating audio endpoints talks to the audio service and costs real
// milliseconds, much worse with USB or Bluetooth devices attached. The menu
// reads this on every open, so the list is cached and invalidated only when a
// device actually arrives or leaves.
const std::vector<Microphone>& AvailableMicrophones();
void                           InvalidateMicrophoneCache();

// Resolves the stored ID against the cached list first, then directly. May
// return nullptr when the device is gone, in which case the menu shows
// "Unavailable" and a recording runs silently.
const Microphone* SelectedMicrophone();

// --- defaults ---
bool IsDefault();
void RestoreDefaults();

} // namespace video
