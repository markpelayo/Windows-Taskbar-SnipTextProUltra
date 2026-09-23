#include "Clipboard.h"

#include "Log.h"
#include "Util.h"

namespace clipboard {
namespace {

// The clipboard is a shared, contended resource: any process can hold it for
// a moment. Retrying turns a visible failure into a pause nobody notices.
bool OpenWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (::OpenClipboard(owner)) return true;
        ::Sleep(20);
    }
    return false;
}

std::wstring ReadBack(HWND owner) {
    if (!OpenWithRetry(owner)) return std::wstring();

    std::wstring out;
    HANDLE handle = ::GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const wchar_t* text = static_cast<const wchar_t*>(::GlobalLock(handle));
        if (text) {
            out.assign(text);
            ::GlobalUnlock(handle);
        }
    }
    ::CloseClipboard();
    return out;
}

} // namespace

bool CopyText(HWND owner, const std::wstring& text) {
    if (text.empty()) {
        log::Write(L"clipboard: refused to write empty text");
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle) return false;

    {
        void* target = ::GlobalLock(handle);
        if (!target) { ::GlobalFree(handle); return false; }
        ::memcpy(target, text.c_str(), bytes);
        ::GlobalUnlock(handle);
    }

    if (!OpenWithRetry(owner)) { ::GlobalFree(handle); return false; }
    ::EmptyClipboard();
    const bool accepted = ::SetClipboardData(CF_UNICODETEXT, handle) != nullptr;
    // On success the clipboard owns the handle and freeing it would be a
    // double free; on failure nobody else will.
    if (!accepted) ::GlobalFree(handle);
    ::CloseClipboard();

    const std::wstring readBack = ReadBack(owner);

    LOG_DEBUG(util::Format(L"clipboard: setData=%d wrote=%zu chars, readBack=%zu chars, sequence=%lu",
                           accepted ? 1 : 0, text.size(), readBack.size(),
                           ::GetClipboardSequenceNumber()));

    if (!readBack.empty() && readBack != text) {
        // A warning only. Something raced us, but the write itself landed.
        log::Write(L"clipboard: WARNING read-back differs from what was written");
    }

    return accepted && !readBack.empty();
}

} // namespace clipboard
