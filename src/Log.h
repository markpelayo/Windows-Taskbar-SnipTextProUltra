// Log.h — plain-text file logger.
//
// One line per capture normally; a full per-stage trace when verbose logging
// is on. The file self-rotates, because a taskbar utility can be left running
// for weeks and verbose growth is otherwise unbounded.
//
// Log at %LOCALAPPDATA%\SnipText\Logs\SnipText.log

#pragma once

#include "framework.h"

namespace log {

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
        if (::log::IsVerbose()) ::log::Write(expr);  \
    } while (0)

} // namespace log
