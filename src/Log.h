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
