// Settings.h — the registry-backed equivalent of UserDefaults.
//
// Everything lives under HKCU\Software\markpelayo\SnipText. The macOS
// original leans on "registered defaults": a value that was never written
// reads as its default, and "restore to default" means *removing* the value
// rather than writing the default back. That distinction is load-bearing —
// the Sanitize menu item decides whether there is anything to do by comparing
// values against their defaults, so writing a default back must be
// indistinguishable from never having written at all.

#pragma once

#include "framework.h"

namespace settings {

extern const wchar_t* const kRegistryPath;   // Software\markpelayo\SnipText

// --- keys ------------------------------------------------------------------
namespace key {
// Checked means "rejoin lines that wrapped"; the default. Deliberately a
// NEW key rather than a reuse of the old "keepLineBreaks": that one meant the
// opposite, and reading it under the new meaning would silently flip the
// behaviour for anyone upgrading.
inline constexpr const wchar_t* kJoinWrappedLines = L"joinWrappedLines";
// "auto" (default), "windows" or "tesseract".
inline constexpr const wchar_t* kOcrEngine        = L"ocrEngine";
inline constexpr const wchar_t* kShutterSound     = L"shutterSound";
// Which built-in tone: 0..4, see capture::shutter. Absent means 0, Classic,
// which is the sound this app has always made.
inline constexpr const wchar_t* kShutterTone      = L"shutterTone";
// Absent or empty means the built-in shutter; otherwise a path to a .wav.
inline constexpr const wchar_t* kShutterSoundPath = L"shutterSoundPath";
inline constexpr const wchar_t* kSaveCaptures     = L"saveCaptures";
inline constexpr const wchar_t* kStartupDelay     = L"startupDelaySeconds";
inline constexpr const wchar_t* kDebugMode        = L"debugMode";

inline constexpr const wchar_t* kScreenshotFolder = L"screenshotFolderPath";
inline constexpr const wchar_t* kTextImageFolder  = L"textImageFolderPath";
inline constexpr const wchar_t* kVideoFolder      = L"videoFolderPath";

inline constexpr const wchar_t* kVideoFrameRate   = L"videoFrameRate";
inline constexpr const wchar_t* kVideoQuality     = L"videoQuality";
// "smaller" (default), "balanced" or "detailed" — a bitrate multiplier.
inline constexpr const wchar_t* kVideoCompression = L"videoCompression";
inline constexpr const wchar_t* kVideoHevc        = L"videoUseHevc";
inline constexpr const wchar_t* kVideoCursor      = L"videoCaptureCursor";
inline constexpr const wchar_t* kVideoClicks      = L"videoCaptureClicks";
inline constexpr const wchar_t* kVideoAudioDevice = L"videoAudioDeviceId";

inline constexpr const wchar_t* kEditorLineWidth  = L"editorLineWidth";
inline constexpr const wchar_t* kEditorColor      = L"editorColorRGBA";
inline constexpr const wchar_t* kEditorTool       = L"editorTool";
} // namespace key

// --- typed access ----------------------------------------------------------
// Each getter takes the default it should fall back to when the value is
// absent or the wrong type, so "absent" and "default" stay the same thing.

bool         GetBool(const wchar_t* name, bool fallback);
void         SetBool(const wchar_t* name, bool value);

int          GetInt(const wchar_t* name, int fallback);
void         SetInt(const wchar_t* name, int value);

double       GetDouble(const wchar_t* name, double fallback);
void         SetDouble(const wchar_t* name, double value);

std::wstring GetString(const wchar_t* name, const std::wstring& fallback = std::wstring());
void         SetString(const wchar_t* name, const std::wstring& value);

void         Remove(const wchar_t* name);
bool         Exists(const wchar_t* name);

// --- Run at Startup --------------------------------------------------------
// HKCU\...\Run, which is the Windows equivalent of a login item. The delay is
// our own setting and applies only when Windows started the app after a boot.

bool IsRunAtStartupEnabled();
bool SetRunAtStartup(bool enabled);

} // namespace settings
