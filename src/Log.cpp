#include "Log.h"
#include "Util.h"

#include <knownfolders.h>
#include <atomic>

namespace logging {
namespace {

constexpr long long kMaxLogBytes      = 512000;
constexpr int       kRotationCheckEvery = 200;

std::atomic<bool> g_verbose{ false };

// All file I/O is serialised behind this lock so writes from the capture
// thread and the main thread cannot interleave mid-line.
Lock&         WriterLock() { static Lock lock; return lock; }
int           g_writeCount = 0;   // only ever touched under WriterLock

std::wstring Directory() {
    return util::JoinPath(util::JoinPath(util::KnownFolder(FOLDERID_LocalAppData, L"AppData\\Local"),
                                         L"SnipText"),
                          L"Logs");
}

void RotateIfNeeded(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return;

    LARGE_INTEGER size{};
    size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
    size.LowPart  = data.nFileSizeLow;
    // No .1 backup and no archive: the log simply starts over. Its whole job
    // is to explain the last few captures.
    if (size.QuadPart > kMaxLogBytes) ::DeleteFileW(path.c_str());
}

void Append(const std::string& utf8) {
    const std::wstring path = FilePath();
    util::EnsureDirectory(Directory());

    ScopedFile file(::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return;   // logging must never throw into the app

    DWORD written = 0;
    ::WriteFile(file.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

} // namespace

std::wstring FilePath() {
    return util::JoinPath(Directory(), L"SnipText.log");
}

bool IsVerbose()             { return g_verbose.load(std::memory_order_relaxed); }
void SetVerbose(bool value)  { g_verbose.store(value, std::memory_order_relaxed); }

void Write(const std::wstring& message) {
    // The timestamp is taken on the caller's thread so it reflects when the
    // event happened, not when the writer got around to it.
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    const std::wstring line = L"[" + util::LogTimestamp(now) + L"] " + message + L"\r\n";
    const std::string  utf8 = util::ToUtf8(line);

    LockGuard guard(WriterLock());
    if (++g_writeCount % kRotationCheckEvery == 0) RotateIfNeeded(FilePath());
    Append(utf8);
}

void StartSession(const std::wstring& note) {
    {
        // Rotation is checked before the marker is written, so a session that
        // starts over the size limit truncates first and the marker becomes
        // line 1 of the fresh file.
        LockGuard guard(WriterLock());
        RotateIfNeeded(FilePath());
    }
    Write(L"———— " + note);
}

void Shutdown() {
    LockGuard guard(WriterLock());
}

} // namespace logging
