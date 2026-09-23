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
    std::wstring DisplayPath(const std::wstring& path) const;

    // Non-recursive, hidden files skipped, extension matched
    // case-insensitively. The extension filter is what stops Sanitize from
    // throwing away other files the user parked in the folder, and it
    // guarantees the confirmation dialog's numbers equal the menu's.
    std::vector<std::wstring> Contents() const;
    int                       Count() const;

    // "SnipText 2026-09-24 at 14.07.03.412.png"
    std::wstring NewFilePath() const;

    void Reveal() const;

    // Writes bytes to a fresh timestamped file. Returns the path, or empty on
    // failure (which is logged, never surfaced — a failed auto-save must not
    // interrupt a capture the user already has in hand).
    std::wstring SaveBytes(const void* data, size_t size) const;

    const std::wstring& Label() const { return label_; }

private:
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
