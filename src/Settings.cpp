#include "Settings.h"
#include "Util.h"

#include <cstdlib>

namespace settings {

const wchar_t* const kRegistryPath = L"Software\\markpelayo\\SnipText";

namespace {

constexpr const wchar_t* kRunKeyPath  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunKeyValue = L"SnipText";

HKEY OpenForRead() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return nullptr;
    }
    return key;
}

HKEY OpenForWrite() {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                          KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return nullptr;
    }
    return key;
}

class KeyHandle {
public:
    explicit KeyHandle(HKEY key) : key_(key) {}
    KeyHandle(const KeyHandle&) = delete;
    KeyHandle& operator=(const KeyHandle&) = delete;
    ~KeyHandle() { if (key_) ::RegCloseKey(key_); }
    HKEY get() const { return key_; }
    explicit operator bool() const { return key_ != nullptr; }
private:
    HKEY key_;
};

} // namespace

int GetInt(const wchar_t* name, int fallback) {
    KeyHandle key(OpenForRead());
    if (!key) return fallback;

    DWORD value = 0, size = sizeof(value), type = 0;
    if (::RegQueryValueExW(key.get(), name, nullptr, &type,
                           reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD) {
        return fallback;
    }
    return static_cast<int>(value);
}

void SetInt(const wchar_t* name, int value) {
    KeyHandle key(OpenForWrite());
    if (!key) return;
    DWORD raw = static_cast<DWORD>(value);
    ::RegSetValueExW(key.get(), name, 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&raw), sizeof(raw));
}

bool GetBool(const wchar_t* name, bool fallback) {
    return GetInt(name, fallback ? 1 : 0) != 0;
}

void SetBool(const wchar_t* name, bool value) {
    SetInt(name, value ? 1 : 0);
}

std::wstring GetString(const wchar_t* name, const std::wstring& fallback) {
    KeyHandle key(OpenForRead());
    if (!key) return fallback;

    DWORD size = 0, type = 0;
    if (::RegQueryValueExW(key.get(), name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size == 0) {
        return fallback;
    }

    // Rounded up: RegQueryValueEx reports bytes, and an odd byte count would
    // otherwise truncate the division and let the registry write one byte
    // past the end of the buffer.
    std::wstring out((size + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
    if (::RegQueryValueExW(key.get(), name, nullptr, nullptr,
                           reinterpret_cast<LPBYTE>(&out[0]), &size) != ERROR_SUCCESS) {
        return fallback;
    }
    // RegQueryValueEx reports the size including the terminator (and may
    // report more than one).
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

void SetString(const wchar_t* name, const std::wstring& value) {
    KeyHandle key(OpenForWrite());
    if (!key) return;
    ::RegSetValueExW(key.get(), name, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(value.c_str()),
                     static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

// Doubles are stored as text rather than as binary, so the registry stays
// readable and a hand-edited value still parses.
double GetDouble(const wchar_t* name, double fallback) {
    const std::wstring raw = GetString(name);
    if (raw.empty()) return fallback;
    wchar_t* end = nullptr;
    double value = ::wcstod(raw.c_str(), &end);
    if (end == raw.c_str()) return fallback;
    return value;
}

void SetDouble(const wchar_t* name, double value) {
    SetString(name, util::Format(L"%.6f", value));
}

void Remove(const wchar_t* name) {
    KeyHandle key(OpenForWrite());
    if (!key) return;
    ::RegDeleteValueW(key.get(), name);
}

bool Exists(const wchar_t* name) {
    KeyHandle key(OpenForRead());
    if (!key) return false;
    return ::RegQueryValueExW(key.get(), name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

// --- Run at Startup --------------------------------------------------------

bool IsRunAtStartupEnabled() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    KeyHandle handle(key);
    return ::RegQueryValueExW(handle.get(), kRunKeyValue, nullptr, nullptr, nullptr, nullptr)
           == ERROR_SUCCESS;
}

bool SetRunAtStartup(bool enabled) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                          KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    KeyHandle handle(key);

    if (!enabled) {
        LONG result = ::RegDeleteValueW(handle.get(), kRunKeyValue);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    // Quoted, because Program Files paths contain spaces.
    const std::wstring command = L"\"" + util::ExecutablePath() + L"\"";
    return ::RegSetValueExW(handle.get(), kRunKeyValue, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(command.c_str()),
                            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)))
           == ERROR_SUCCESS;
}

} // namespace settings
