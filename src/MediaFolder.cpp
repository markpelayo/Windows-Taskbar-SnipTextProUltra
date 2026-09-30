#include "MediaFolder.h"

#include "Settings.h"
#include "Util.h"

#include <knownfolders.h>

namespace {
const KNOWNFOLDERID kPictures = FOLDERID_Pictures;
const KNOWNFOLDERID kVideos   = FOLDERID_Videos;
} // namespace

MediaFolder::MediaFolder(const wchar_t* settingsKey, const wchar_t* folderName,
                         const KNOWNFOLDERID* systemFolder, const wchar_t* homeFallback,
                         const wchar_t* extension, const wchar_t* label)
    : settingsKey_(settingsKey),
      folderName_(folderName),
      systemFolder_(systemFolder),
      homeFallback_(homeFallback),
      extension_(extension),
      label_(label) {}

MediaFolder& MediaFolder::Screenshots() {
    static MediaFolder folder(settings::key::kScreenshotFolder, L"SnipTextProUltra_Screenshot_Images",
                              &kPictures, L"Pictures", L"png", L"screenshots");
    return folder;
}

MediaFolder& MediaFolder::TextImages() {
    static MediaFolder folder(settings::key::kTextImageFolder, L"SnipTextProUltra_ScreenshotToText_Images",
                              &kPictures, L"Pictures", L"png", L"text images");
    return folder;
}

MediaFolder& MediaFolder::Videos() {
    static MediaFolder folder(settings::key::kVideoFolder, L"SnipTextProUltra_Videos",
                              &kVideos, L"Videos", L"mp4", L"videos");
    return folder;
}

std::wstring MediaFolder::DefaultDirectory() const {
    return util::JoinPath(util::KnownFolder(*systemFolder_, homeFallback_.c_str()), folderName_);
}

std::wstring MediaFolder::Directory() const {
    const std::wstring override = settings::GetString(settingsKey_);
    if (!override.empty()) return override;
    return DefaultDirectory();
}

bool MediaFolder::IsUsingDefaultDirectory() const {
    // Absent and empty are treated identically, so a stored empty string can
    // never masquerade as a custom folder.
    return settings::GetString(settingsKey_).empty();
}

void MediaFolder::SetDirectory(const std::wstring& path) {
    if (path.empty()) {
        settings::Remove(settingsKey_);
    } else {
        settings::SetString(settingsKey_, path);
    }
    EnsureDirectoryExists();
}

bool MediaFolder::EnsureDirectoryExists() const {
    const std::wstring path = Directory();
    if (util::EnsureDirectory(path)) return true;
    return false;
}

std::wstring MediaFolder::DisplayPath() const {
    return util::DisplayPath(Directory());
}

std::vector<std::wstring> MediaFolder::Contents() const {
    std::vector<std::wstring> out;
    const std::wstring directory = Directory();
    const std::wstring pattern   = util::JoinPath(directory, L"*");

    WIN32_FIND_DATAW found{};
    ScopedFind search(::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found,
                                         FindExSearchNameMatch, nullptr, 0));
    if (!search) return out;   // a missing folder is simply empty, never an error

    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    continue;
        const std::wstring name = found.cFileName;
        if (util::FileExtensionLower(name) != extension_) continue;
        out.push_back(util::JoinPath(directory, name));
    } while (::FindNextFileW(search.get(), &found));

    return out;
}

int MediaFolder::Count() const {
    const std::wstring directory = Directory();

    // One attribute query rather than a directory walk: sub-millisecond, and
    // a single round trip even over SMB.
    // A stamp is only usable if it is actually a stamp. GetFileAttributesExW
    // succeeding is not enough: FAT and exFAT volume roots have no directory
    // entry and report zeroed times, and some SMB, WebDAV and MTP redirectors
    // report zero or a value that never advances. Keyed on a constant, the
    // cache would freeze the count for the life of the process — the menu
    // showing a number that never changes and never matches the Sanitize
    // dialog. A zero stamp means "count every time", which is merely slow.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    const bool stamped =
        ::GetFileAttributesExW(directory.c_str(), GetFileExInfoStandard, &info) != 0 &&
        (info.ftLastWriteTime.dwLowDateTime | info.ftLastWriteTime.dwHighDateTime) != 0;

    if (stamped && countedFiles_ >= 0 && countedDirectory_ == directory &&
        info.ftLastWriteTime.dwLowDateTime  == countedStamp_.dwLowDateTime &&
        info.ftLastWriteTime.dwHighDateTime == countedStamp_.dwHighDateTime) {
        return countedFiles_;
    }

    // Counts with the same three filters Contents() uses, deliberately
    // duplicated rather than calling it: Contents() joins a full path per
    // entry, which is a couple of thousand heap allocations and a few hundred
    // kilobytes of churn for a number.
    int files = 0;
    const std::wstring pattern = util::JoinPath(directory, L"*");

    WIN32_FIND_DATAW found{};
    ScopedFind search(::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found,
                                         FindExSearchNameMatch, nullptr, 0));
    if (search) {
        do {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    continue;
            if (util::FileExtensionLower(found.cFileName) != extension_) continue;
            ++files;
        } while (::FindNextFileW(search.get(), &found));
    }

    // Only cached when there is a stamp to validate it against. Without one
    // the next call counts again, which is correct rather than stale.
    if (stamped) {
        countedDirectory_ = directory;
        countedStamp_     = info.ftLastWriteTime;
        countedFiles_     = files;
    } else {
        countedFiles_ = -1;
    }
    return files;
}

std::wstring MediaFolder::NewFilePath() const {
    const std::wstring name = L"SnipTextProUltra " + util::FileNameTimestamp() + L"." + extension_;
    return util::JoinPath(Directory(), name);
}

void MediaFolder::Reveal() const {
    EnsureDirectoryExists();
    const std::wstring path = Directory();
    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring MediaFolder::SaveBytes(const void* data, size_t size) const {
    if (!data || size == 0) return std::wstring();

    // The path is resolved before the write, so a folder change that lands
    // mid-write cannot redirect the file.
    const std::wstring path = NewFilePath();
    if (!EnsureDirectoryExists()) return std::wstring();

    ScopedFile file(::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return std::wstring();
    }

    const BYTE* cursor = static_cast<const BYTE*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        DWORD chunk   = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(1u << 20)));
        DWORD written = 0;
        if (!::WriteFile(file.get(), cursor, chunk, &written, nullptr) || written == 0) {
            // Close the handle, then remove the partial file. A truncated
            // PNG is not a picture, but it is still a file: it would count
            // toward the folder total in the menu and appear in the
            // Sanitize list, so the user is told they have a screenshot
            // that will not open. Same reasoning as the recorder's failure
            // paths, which this now matches.
            file.reset();
            ::DeleteFileW(path.c_str());
            return std::wstring();
        }
        cursor    += written;
        remaining -= written;
    }

    // We just changed this folder, so say so rather than waiting for the
    // directory timestamp to catch up — see InvalidateCount in the header.
    InvalidateCount();
    return path;
}

namespace media {
bool RecycleFiles(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return true;

    // SHFileOperation wants a double-null-terminated list of null-terminated
    // paths, so build one buffer rather than calling it once per file.
    std::wstring buffer;
    for (const std::wstring& path : paths) {
        buffer.append(path);
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = buffer.c_str();
    // ALLOWUNDO is the whole point: these go to the Recycle Bin, so the user
    // can put them back. NOCONFIRMATION because we already asked, once, with
    // a dialog that itemised exactly what would happen.
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

    int result = ::SHFileOperationW(&op);
    return result == 0 && !op.fAnyOperationsAborted;
}

} // namespace media
