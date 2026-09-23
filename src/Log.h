// Log.h — plain-text file logger.
//
// One line per capture normally; a full per-stage trace when verbose logging
// is on. The file self-rotates, because a taskbar utility can be left running
// for weeks and verbose growth is otherwise unbounded.
//
// Log at %LOCALAPPDATA%\SnipText\Logs\SnipText.log
//
// The namespace is `logging`, not the obvious `log`, because `::log` is the
// C math function and <math.h> arrives transitively through the Windows
// headers in every translation unit. MSVC rejects the collision outright:
//   error C2757: 'log': a symbol with this name already exists and therefore
//   this name cannot be used as a namespace name
// Please do not rename it back.

#pragma once

#include "framework.h"

namespace logging {

void         StartSession(const std::wstring& note);
void         Write(const std::wstring& message);
std::wstring FilePath();

// ---------------------------------------------------------------------------
// TEMPORARY — the 1.x shakedown switch.
//
// This build has never run on real hardware, so the log is the only thing
// that can explain a misbehaviour to someone who wasn't sitting in front of
// it. Verbose logging is therefore ON by default, and a startup banner
// records the machine's Windows build, display layout and OCR availability.
//
// Neither is free: verbose mode writes several lines per capture, and the
// banner costs a few milliseconds at launch.
//
// To turn it off on one machine without rebuilding:
//   reg add "HKCU\Software\markpelayo\SnipText" /v debugMode /t REG_DWORD /d 0 /f
//
// TO REVERT once the app is polished: set this to false, and delete the
// WriteStartupDiagnostics() call in App::Run(). Nothing else depends on it.
// ---------------------------------------------------------------------------
inline constexpr bool kVerboseByDefault = true;

bool IsVerbose();
void SetVerbose(bool verbose);

void Shutdown();

// Verbose call sites do real work to build their message — string
// interpolation, substring previews, counts. This macro short-circuits before
// evaluating any of it, so a disabled debug line costs one atomic load.
#define LOG_DEBUG(expr)                              \
    do {                                             \
        if (::logging::IsVerbose()) ::logging::Write(expr);  \
    } while (0)

} // namespace logging
