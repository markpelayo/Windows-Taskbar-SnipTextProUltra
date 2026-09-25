#include "Capture.h"

#include "Log.h"
#include "Settings.h"
#include "Util.h"

#include <mmsystem.h>

namespace capture {
namespace {

std::unique_ptr<Bitmap> GrabRect(const RECT& bounds) {
    const int width  = util::RectWidth(bounds);
    const int height = util::RectHeight(bounds);
    if (width <= 0 || height <= 0) return nullptr;

    auto image = Bitmap::Create(width, height);
    if (!image) {
        logging::Write(util::Format(L"capture: couldn't allocate a %dx%d image", width, height));
        return nullptr;
    }

    HDC target = image->MemoryDC();
    if (!target) return nullptr;

    WindowDC screen(nullptr);   // the whole virtual desktop
    if (!screen) return nullptr;

    // CAPTUREBLT is what pulls in layered windows — without it a translucent
    // window over the region leaves a hole in the capture.
    if (!::BitBlt(target, 0, 0, width, height, screen.get(), bounds.left, bounds.top,
                  SRCCOPY | CAPTUREBLT)) {
        logging::Write(L"capture: BitBlt failed");
        return nullptr;
    }

    // BitBlt leaves the alpha channel undefined. An image that reaches the
    // clipboard with zero alpha pastes as an invisible rectangle, and OCR
    // reads nothing from it.
    image->MakeOpaque();
    return image;
}

} // namespace

const wchar_t* ModeLabel(Mode mode) {
    return mode == Mode::Region ? L"region" : L"full screen";
}

std::unique_ptr<Bitmap> GrabMonitor(HMONITOR monitor) {
    return GrabRect(util::MonitorBounds(monitor));
}

std::unique_ptr<Bitmap> GrabVirtualDesktop(RECT* bounds) {
    RECT desktop{};
    desktop.left   = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    desktop.top    = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    desktop.right  = desktop.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    desktop.bottom = desktop.top  + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (bounds) *bounds = desktop;
    return GrabRect(desktop);
}

namespace {

// --- the built-in shutter --------------------------------------------------
//
// A real camera shutter is two transients about 70 ms apart — the mirror
// going up, then the blades closing — each a short noise burst with a fast
// decay and a little low-frequency body behind it. Synthesising that is a few
// lines and sounds right; the alternative was a system alias that says
// "error" to anyone listening.

constexpr int   kSampleRate = 44100;
constexpr int   kChannels   = 1;
constexpr int   kBitsPerSample = 16;
constexpr double kDurationSeconds = 0.20;

// Deterministic, so the sound is identical on every machine and every run.
// rand() would be neither, and seeding it would disturb the caller's.
struct Noise {
    unsigned int state = 0x2F6E2B1u;
    double Next() {
        state = state * 1664525u + 1013904223u;
        return (static_cast<double>(state >> 8) / 8388608.0) - 1.0;   // -1..1
    }
};

std::vector<BYTE> BuildShutterWav() {
    const int frames = static_cast<int>(kSampleRate * kDurationSeconds);
    std::vector<INT16> samples(static_cast<size_t>(frames), 0);

    Noise noise;
    double lowpass = 0.0;   // one-pole, takes the fizz off the noise

    // (start seconds, amplitude, decay time constant)
    struct Click { double at; double amplitude; double decay; };
    const Click clicks[2] = { { 0.000, 1.00, 0.013 },
                              { 0.072, 0.72, 0.020 } };

    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        double value = 0.0;

        for (const Click& click : clicks) {
            if (t < click.at) continue;
            const double age = t - click.at;
            const double envelope = std::exp(-age / click.decay);

            // The snap: filtered noise.
            const double raw = noise.Next();
            lowpass += (raw - lowpass) * 0.45;
            value += lowpass * envelope * click.amplitude * 0.85;

            // The thunk: a fast-decaying low tone underneath, which is what
            // makes it read as mechanical rather than as static.
            const double body = std::sin(2.0 * 3.14159265358979 * 190.0 * age);
            value += body * std::exp(-age / 0.010) * click.amplitude * 0.30;
        }

        value = (std::max)(-1.0, (std::min)(1.0, value));
        samples[static_cast<size_t>(i)] = static_cast<INT16>(value * 26000.0);
    }

    // A 10 ms fade at the end, so stopping mid-cycle doesn't click.
    const int fade = kSampleRate / 100;
    for (int i = 0; i < fade && i < frames; ++i) {
        const size_t index = static_cast<size_t>(frames - 1 - i);
        samples[index] = static_cast<INT16>(samples[index] * (static_cast<double>(i) / fade));
    }

    // --- wrap it in a RIFF/WAVE container ---
    const DWORD dataBytes = static_cast<DWORD>(samples.size() * sizeof(INT16));
    const DWORD byteRate  = kSampleRate * kChannels * (kBitsPerSample / 8);

    std::vector<BYTE> wav;
    wav.reserve(44 + dataBytes);
    auto push32 = [&wav](DWORD value) {
        wav.push_back(static_cast<BYTE>(value & 0xFF));
        wav.push_back(static_cast<BYTE>((value >> 8) & 0xFF));
        wav.push_back(static_cast<BYTE>((value >> 16) & 0xFF));
        wav.push_back(static_cast<BYTE>((value >> 24) & 0xFF));
    };
    auto push16 = [&wav](WORD value) {
        wav.push_back(static_cast<BYTE>(value & 0xFF));
        wav.push_back(static_cast<BYTE>((value >> 8) & 0xFF));
    };
    auto pushTag = [&wav](const char* tag) {
        for (int i = 0; i < 4; ++i) wav.push_back(static_cast<BYTE>(tag[i]));
    };

    pushTag("RIFF");  push32(36 + dataBytes);
    pushTag("WAVE");
    pushTag("fmt ");  push32(16);
    push16(1);                                    // PCM
    push16(static_cast<WORD>(kChannels));
    push32(kSampleRate);
    push32(byteRate);
    push16(static_cast<WORD>(kChannels * (kBitsPerSample / 8)));
    push16(static_cast<WORD>(kBitsPerSample));
    pushTag("data");  push32(dataBytes);

    const BYTE* raw = reinterpret_cast<const BYTE*>(samples.data());
    wav.insert(wav.end(), raw, raw + dataBytes);
    return wav;
}

// Built once and intentionally never destroyed. PlaySound with SND_MEMORY
// plays asynchronously straight out of this buffer from winmm's own thread,
// so it has to outlive not just the call but the process's static-destruction
// phase — quitting right after a capture would otherwise free it mid-sound.
const std::vector<BYTE>& ShutterWav() {
    static const std::vector<BYTE>* wav = new std::vector<BYTE>(BuildShutterWav());
    return *wav;
}

void PlayConfiguredShutter() {
    const std::wstring custom = settings::GetString(settings::key::kShutterSoundPath);
    if (!custom.empty()) {
        if (::PlaySoundW(custom.c_str(), nullptr,
                         SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
            return;
        }
        // Missing or unplayable: fall through to the built-in one rather than
        // leaving the capture silent. Logged once rather than on every
        // capture — a line per screenshot would bury everything else.
        static bool warned = false;
        if (!warned) {
            warned = true;
            logging::Write(L"shutter: couldn't play " + custom
                           + L", using the built-in sound");
        }
    }

    const std::vector<BYTE>& wav = ShutterWav();
    if (wav.empty()) { ::MessageBeep(MB_OK); return; }
    ::PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC);
}

} // namespace

void PlayShutter() {
    PlayConfiguredShutter();
}

void PreviewShutter() {
    PlayConfiguredShutter();
}

} // namespace capture
