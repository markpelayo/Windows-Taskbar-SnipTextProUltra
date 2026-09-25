#include "VideoSettings.h"

#include "Log.h"
#include "Settings.h"
#include "Util.h"

#include <mmdeviceapi.h>
// initguid.h must come first, and propkeydef.h must be re-included after it:
// shlobj.h has already pulled propkeydef.h in without INITGUID, which leaves
// PKEY_Device_FriendlyName declared but never defined, and the link then
// fails on a symbol no linked library carries.
#include <initguid.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace video {

const int kFrameRateChoices[4] = { 15, 24, 30, 60 };

namespace {

// nullptr means "not cached". An empty vector is a valid cached answer and
// must not trigger a rescan — a machine with no microphone would otherwise
// re-enumerate on every menu open.
std::unique_ptr<std::vector<Microphone>> g_microphones;

const wchar_t* QualityKeyValue(Quality quality) {
    switch (quality) {
    case Quality::Medium: return L"medium";
    case Quality::Low:    return L"low";
    default:              return L"high";
    }
}

} // namespace

// --- frame rate ------------------------------------------------------------

int FrameRate() {
    const int stored = settings::GetInt(settings::key::kVideoFrameRate, kDefaultFrameRate);
    for (int choice : kFrameRateChoices) {
        if (stored == choice) return stored;
    }
    // Anything unrecognised — absent, hand-edited, written by an older
    // version — coerces to the default rather than being honoured.
    return kDefaultFrameRate;
}

void SetFrameRate(int fps) {
    settings::SetInt(settings::key::kVideoFrameRate, fps);
}

// --- quality ---------------------------------------------------------------

Quality CurrentQuality() {
    const std::wstring stored = settings::GetString(settings::key::kVideoQuality, L"high");
    if (stored == L"medium") return Quality::Medium;
    if (stored == L"low")    return Quality::Low;
    return Quality::High;
}

void SetQuality(Quality quality) {
    settings::SetString(settings::key::kVideoQuality, QualityKeyValue(quality));
}

double QualityScale(Quality quality) {
    switch (quality) {
    case Quality::Medium: return 0.75;
    case Quality::Low:    return 0.5;
    default:              return 1.0;
    }
}

double CurrentQualityScale() { return QualityScale(CurrentQuality()); }

const wchar_t* QualityTitle(Quality quality) {
    switch (quality) {
    case Quality::Medium: return L"Medium — 75% scale";
    case Quality::Low:    return L"Low — 50% scale";
    default:              return L"High — full resolution";
    }
}

const wchar_t* QualityShortTitle(Quality quality) {
    switch (quality) {
    case Quality::Medium: return L"Medium";
    case Quality::Low:    return L"Low";
    default:              return L"High";
    }
}

// --- compression -----------------------------------------------------------
//
// Smaller is the default. That is a change of behaviour, and it is deliberate:
// the old fixed multiplier was tuned to be safe for any content, and screen
// recording is not any content. A desktop is mostly unchanged from frame to
// frame, which is the case H.264 handles best, so the bits that setting spent
// were going into encoding a static background very precisely.

Compression CurrentCompression() {
    const std::wstring stored = settings::GetString(settings::key::kVideoCompression, L"smaller");
    if (stored == L"balanced") return Compression::Balanced;
    if (stored == L"detailed") return Compression::Detailed;
    return Compression::Smaller;
}

void SetCompression(Compression value) {
    const wchar_t* name = L"smaller";
    if (value == Compression::Balanced) name = L"balanced";
    if (value == Compression::Detailed) name = L"detailed";
    settings::SetString(settings::key::kVideoCompression, name);
}

double CompressionScale(Compression value) {
    switch (value) {
    // 1.0 is what every recording before this used, so Balanced is not a new
    // setting to be judged on its own — it is the old behaviour, kept for
    // anyone who was happy with it.
    case Compression::Balanced: return 1.0;
    case Compression::Detailed: return 1.6;
    default:                    return 0.55;
    }
}

double CurrentCompressionScale() { return CompressionScale(CurrentCompression()); }

const wchar_t* CompressionTitle(Compression value) {
    switch (value) {
    case Compression::Balanced: return L"Balanced — the previous default";
    case Compression::Detailed: return L"Detailed — for fine text and gradients";
    default:                    return L"Smaller — about half the size";
    }
}

const wchar_t* CompressionShortTitle(Compression value) {
    switch (value) {
    case Compression::Balanced: return L"Balanced";
    case Compression::Detailed: return L"Detailed";
    default:                    return L"Smaller";
    }
}

bool UsesHevc()          { return settings::GetBool(settings::key::kVideoHevc, false); }
void SetUsesHevc(bool v) { settings::SetBool(settings::key::kVideoHevc, v); }

// --- cursor and clicks -----------------------------------------------------

bool CapturesCursor()            { return settings::GetBool(settings::key::kVideoCursor, true); }
void SetCapturesCursor(bool v)   { settings::SetBool(settings::key::kVideoCursor, v); }
bool CapturesClicks()            { return settings::GetBool(settings::key::kVideoClicks, true); }
void SetCapturesClicks(bool v)   { settings::SetBool(settings::key::kVideoClicks, v); }

// --- audio -----------------------------------------------------------------

std::wstring AudioDeviceId() {
    return settings::GetString(settings::key::kVideoAudioDevice);
}

void SetAudioDeviceId(const std::wstring& id) {
    if (id.empty()) {
        // Removed, not stored empty, so "no audio" is the absence of a
        // setting rather than a setting with an empty value.
        settings::Remove(settings::key::kVideoAudioDevice);
    } else {
        settings::SetString(settings::key::kVideoAudioDevice, id);
    }
}

void InvalidateMicrophoneCache() { g_microphones.reset(); }

const std::vector<Microphone>& AvailableMicrophones() {
    if (g_microphones) return *g_microphones;
    g_microphones = std::make_unique<std::vector<Microphone>>();

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator)))) {
        return *g_microphones;
    }

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection))) {
        return *g_microphones;
    }

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device)) || !device) continue;

        Microphone microphone;

        LPWSTR rawId = nullptr;
        if (SUCCEEDED(device->GetId(&rawId)) && rawId) {
            microphone.id.assign(rawId);
            ::CoTaskMemFree(rawId);
        }
        if (microphone.id.empty()) continue;

        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) && properties) {
            PROPVARIANT name{};
            ::PropVariantInit(&name);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
                name.vt == VT_LPWSTR && name.pwszVal) {
                microphone.name.assign(name.pwszVal);
            }
            ::PropVariantClear(&name);
        }
        if (microphone.name.empty()) microphone.name = L"Microphone";

        g_microphones->push_back(std::move(microphone));
    }

    return *g_microphones;
}

const Microphone* SelectedMicrophone() {
    const std::wstring id = AudioDeviceId();
    if (id.empty()) return nullptr;

    for (const Microphone& microphone : AvailableMicrophones()) {
        if (microphone.id == id) return &microphone;
    }
    return nullptr;   // stored but gone: the menu says "Unavailable"
}

// --- defaults --------------------------------------------------------------

bool IsDefault() {
    return FrameRate() == kDefaultFrameRate
        && CurrentQuality() == Quality::High
        && CurrentCompression() == Compression::Smaller
        && !UsesHevc()
        && CapturesCursor()
        && CapturesClicks()
        && AudioDeviceId().empty();
}

void RestoreDefaults() {
    settings::Remove(settings::key::kVideoFrameRate);
    settings::Remove(settings::key::kVideoQuality);
    settings::Remove(settings::key::kVideoCompression);
    settings::Remove(settings::key::kVideoHevc);
    settings::Remove(settings::key::kVideoCursor);
    settings::Remove(settings::key::kVideoClicks);
    settings::Remove(settings::key::kVideoAudioDevice);
}

} // namespace video
