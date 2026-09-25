// VideoSettings.h — frame rate, quality, cursor, clicks, audio input.
//
// Screen Recording Settings lives in the flyout menu rather than behind a
// gear button, so every setting in the app is in one place.

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

// --- compression ------------------------------------------------------------
//
// On the container question, because it comes up and the answer is
// counter-intuitive: MKV and AVI are not on offer, and would not help if they
// were.
//
//   1. Media Foundation cannot write either one. MFCreateSinkWriterFromURL
//      picks a media sink from the file extension, and the sinks that ship
//      with Windows are MPEG-4 (.mp4, .m4v, .3gp) and ASF (.wmv, .asf). There
//      is an MKV *source* — Windows 10 can play Matroska — but no MKV sink,
//      and no AVI sink in either direction. Writing either one means embedding
//      a third-party muxer, and a static FFmpeg is tens of megabytes against
//      this program's whole budget.
//
//   2. A container does not compress anything. It is an index and a wrapper
//      around streams that are already encoded. Remuxing the same H.264 stream
//      from MP4 to MKV changes the file by a few kilobytes of header over an
//      entire recording — well under a tenth of a percent. The idea that MKV
//      is smaller comes from MKV files often being encoded differently, not
//      from the container.
//
// What actually sets the size is the codec and the bitrate, so those are what
// this exposes.

enum class Compression { Smaller, Balanced, Detailed };

// Multiplies the pixel-rate-derived bitrate. Screen content is mostly static
// between frames and compresses far better than camera footage, so Smaller is
// a sensible default rather than a compromise — it is the setting that was
// missing, not a downgrade.
Compression    CurrentCompression();
void           SetCompression(Compression value);
double         CompressionScale(Compression value);
double         CurrentCompressionScale();
const wchar_t* CompressionTitle(Compression value);       // "Smaller — about half the size"
const wchar_t* CompressionShortTitle(Compression value);  // "Smaller"

// H.265 encodes the same picture in roughly half the bits, but it needs an
// HEVC encoder present to write and a reasonably modern player to open, and
// neither is guaranteed. Off by default for that reason: a recording that
// will not play on the machine you sent it to is not a smaller file, it is a
// broken one. The recorder falls back to H.264 when no HEVC encoder exists,
// so switching this on can never leave you with no recording at all.
bool UsesHevc();
void SetUsesHevc(bool value);

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
