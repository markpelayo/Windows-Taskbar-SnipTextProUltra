// MediaFolder.h — one output folder, with its override setting and defaults.
//
// Three instances exist: screenshots, screenshot-to-text source images, and
// videos. Each can be pointed somewhere else independently.
//
// Nothing is ever auto-deleted. These are the user's own files in their
// Pictures and Videos folders; pruning them without asking would be data loss.

#pragma once

#include "framework.h"

class MediaFolder {
public:
    // The three instances. Defined in the .cpp so the folder names and
    // defaults keys live in exactly one place.
    static MediaFolder& Screenshots();
    static MediaFolder& TextImages();
    static MediaFolder& Videos();

    std::wstring DefaultDirectory() const;
    std::wstring Directory() const;

    // Passing an empty path removes the override, which is not the same as
    // storing an empty string: "reset to default" must leave no trace.
    void SetDirectory(const std::wstring& path);
    bool IsUsingDefaultDirectory() const;

    bool EnsureDirectoryExists() const;

    // ~-abbreviated, for menu labels and tooltips.
    std::wstring DisplayPath() const;

    // Non-recursive, hidden files skipped, extension matched
    // case-insensitively. The extension filter is what stops Sanitize from
    // throwing away other files the user parked in the folder, and it
    // guarantees the confirmation dialog's numbers equal the menu's.
    std::vector<std::wstring> Contents() const;

    // Same predicate as Contents(), same number — but it counts without
    // building the paths, and it caches.
    //
    // The menu asks all three folders for this on EVERY open, including the
    // one shown at launch. Re-enumerating is fine on a warm local disk and
    // expensive everywhere else: the DEFAULT folders are Pictures and Videos,
    // which Windows 11 frequently redirects into OneDrive, where a directory
    // walk is a network round trip. Measured at 100-600ms for three folders
    // with a couple of thousand files — paid every time the menu opened.
    //
    // The cache is keyed on the directory's last-write time, which moves when
    // a file is added, removed or renamed — so a file the user deleted in
    // Explorer invalidates it. Two honest caveats: a file merely modified in
    // place does not move the stamp, and does not change the count either;
    // and a zero or never-advancing stamp, which some non-NTFS and network
    // paths report, is rejected outright so those folders simply count every
    // time. Changes this program makes itself do not wait for the
    // timestamp — see InvalidateCount.
    int Count() const;

    // "SnipTextProUltra 2026-09-24 at 14.07.03.412.png"
    std::wstring NewFilePath() const;

    void Reveal() const;

    // Writes bytes to a fresh timestamped file. Returns the path, or empty on
    // failure (which is logged, never surfaced — a failed auto-save must not
    // interrupt a capture the user already has in hand).
    std::wstring SaveBytes(const void* data, size_t size) const;

    // Drops the cached count. Called whenever THIS program changes the
    // folder, rather than trusting the directory timestamp to notice.
    //
    // Necessary because a directory's timestamp lives in its parent's entry
    // and is flushed lazily — the documented "information may not be current"
    // caveat. Waiting for it would resurrect precisely the bug the live counts
    // were built to avoid: a screenshot writes a file and the menu's number
    // does not move until something unrelated happens. The timestamp is left
    // to do what it is good at, which is catching changes made from outside.
    void InvalidateCount() const { countedFiles_ = -1; }

    const std::wstring& Label() const { return label_; }

private:

    // Cached count, and what it was valid for. Mutable because Count() is
    // logically a query; callers treat it as one and it is const everywhere.
    mutable std::wstring  countedDirectory_;
    mutable FILETIME      countedStamp_{};
    mutable int           countedFiles_ = -1;   // -1 means nothing cached yet

    MediaFolder(const wchar_t* settingsKey, const wchar_t* folderName, const KNOWNFOLDERID* systemFolder,
                const wchar_t* homeFallback, const wchar_t* extension, const wchar_t* label);

    const wchar_t*        settingsKey_;
    std::wstring          folderName_;
    const KNOWNFOLDERID*  systemFolder_;
    std::wstring          homeFallback_;
    std::wstring          extension_;
    std::wstring          label_;
};

namespace media {

// Moves the given files to the Recycle Bin. Never a hard delete: a single
// menu click should not put a folder of screenshots beyond recovery.
bool RecycleFiles(const std::vector<std::wstring>& paths);

} // namespace media
