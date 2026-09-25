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

// --- the built-in shutters -------------------------------------------------
//
// A real camera shutter is two transients about 70 ms apart — the mirror
// going up, then the blades closing — each a short noise burst with a fast
// decay and a little low-frequency body behind it. Synthesising that is a few
// lines and sounds right; the alternative was a system alias that says
// "error" to anyone listening.
//
// There are five of them now, and they are all the same generator driven by
// the table below rather than five copies of the loop. That is not only less
// code: it is the only way five sounds stay consistent in level and length as
// any of them is adjusted, and it makes a sixth a single row.
//
// On the "macOS-like" one, plainly: Apple's screenshot sound is their audio
// asset and is not something to copy into this binary. Tone 2 is an original
// synthesis with a similar *character* — bright, tight, a quick two-stage
// click rather than a heavy mechanical thunk. It is a family resemblance, not
// a reproduction.

constexpr int   kSampleRate = 44100;
constexpr int   kChannels   = 1;
constexpr int   kBitsPerSample = 16;

// Deterministic, so the sound is identical on every machine and every run.
// rand() would be neither, and seeding it would disturb the caller's.
struct Noise {
    unsigned int state = 0x2F6E2B1u;
    double Next() {
        state = state * 1664525u + 1013904223u;
        return (static_cast<double>(state >> 8) / 8388608.0) - 1.0;   // -1..1
    }
};

// One transient: when it starts, how loud, and how fast it dies away.
struct Voice { double at; double amplitude; double decay; };

struct Recipe {
    const wchar_t* name;
    double duration;      // seconds
    double brightness;    // one-pole coefficient, 0..1; higher is brighter
    double noiseAmount;   // how much of the snap is filtered noise
    double bodyHz;        // the low tone that makes it read as mechanical
    double bodyDecay;
    double bodyAmount;
    double gain;          // peak sample value, out of 32767
    int    voiceCount;
    Voice  voices[3];
};

// Tone 0 reproduces the original sound exactly — same voices, same
// coefficients, same order of noise draws. Anyone who liked it keeps it.
const Recipe kRecipes[] = {
    // 0 — Classic. The original: SLR-ish, medium weight, two stages 72 ms apart.
    { L"Classic",     0.20, 0.45, 0.85, 190.0, 0.010, 0.30, 26000.0, 2,
      { { 0.000, 1.00, 0.013 }, { 0.072, 0.72, 0.020 } } },

    // 1 — SLR Camera. Heavier and darker, with a third quieter transient for
    // the mirror dropping back. This is the "proper camera" one.
    { L"SLR Camera",  0.30, 0.28, 0.80, 118.0, 0.020, 0.50, 26000.0, 3,
      { { 0.000, 1.00, 0.016 }, { 0.095, 0.88, 0.030 }, { 0.150, 0.34, 0.014 } } },

    // 2 — Aperture. Bright and tight, the two stages only 36 ms apart so they
    // read as one quick "k-chk" rather than two separate knocks.
    { L"Aperture",    0.16, 0.78, 0.92, 320.0, 0.005, 0.16, 24000.0, 2,
      { { 0.000, 1.00, 0.008 }, { 0.036, 0.58, 0.011 } } },

    // 3 — Soft Click. One quiet tick, for open-plan offices and recordings
    // where a full shutter on every capture is too much.
    { L"Soft Click",  0.10, 0.34, 0.60, 230.0, 0.008, 0.26, 14000.0, 1,
      { { 0.000, 0.85, 0.011 } } },

    // 4 — Snap. A single crisp high click with almost no body: the shortest
    // possible "that happened".
    { L"Snap",        0.09, 0.88, 1.00, 430.0, 0.004, 0.10, 25000.0, 1,
      { { 0.000, 1.00, 0.006 } } },
};

static_assert(sizeof(kRecipes) / sizeof(kRecipes[0]) == shutter::kToneCount,
              "kToneCount and the recipe table have to agree");

std::vector<BYTE> BuildShutterWav(const Recipe& recipe) {
    const int frames = static_cast<int>(kSampleRate * recipe.duration);
    std::vector<INT16> samples(static_cast<size_t>(frames), 0);

    Noise noise;
    double lowpass = 0.0;   // one-pole, takes the fizz off the noise

    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        double value = 0.0;

        for (int v = 0; v < recipe.voiceCount; ++v) {
            const Voice& click = recipe.voices[v];
            if (t < click.at) continue;
            const double age = t - click.at;
            const double envelope = std::exp(-age / click.decay);

            // The snap: filtered noise.
            const double raw = noise.Next();
            lowpass += (raw - lowpass) * recipe.brightness;
            value += lowpass * envelope * click.amplitude * recipe.noiseAmount;

            // The thunk: a fast-decaying low tone underneath, which is what
            // makes it read as mechanical rather than as static.
            const double body = std::sin(2.0 * 3.14159265358979 * recipe.bodyHz * age);
            value += body * std::exp(-age / recipe.bodyDecay)
                          * click.amplitude * recipe.bodyAmount;
        }

        value = (std::max)(-1.0, (std::min)(1.0, value));
        samples[static_cast<size_t>(i)] = static_cast<INT16>(value * recipe.gain);
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

// Built on first use and intentionally never destroyed. PlaySound with
// SND_MEMORY plays asynchronously straight out of this buffer from winmm's own
// thread, so it has to outlive not just the call but the process's
// static-destruction phase — quitting right after a capture would otherwise
// free it mid-sound.
//
// Lazily, one slot per tone: a tone that is never chosen is never generated,
// so the usual cost is one buffer of about 18 KB for the whole run rather than
// five. Touched only from the UI thread.
const std::vector<BYTE>& ShutterWav(int tone) {
    static const std::vector<BYTE>* cache[shutter::kToneCount] = {};
    if (tone < 0 || tone >= shutter::kToneCount) tone = 0;
    if (!cache[tone]) {
        cache[tone] = new std::vector<BYTE>(BuildShutterWav(kRecipes[tone]));
    }
    return *cache[tone];
}

void PlayBuiltIn(int tone) {
    const std::vector<BYTE>& wav = ShutterWav(tone);
    if (wav.empty()) { ::MessageBeep(MB_OK); return; }
    ::PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC);
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

    PlayBuiltIn(shutter::CurrentTone());
}

} // namespace

void PlayShutter() {
    PlayConfiguredShutter();
}

void PreviewShutter() {
    PlayConfiguredShutter();
}

namespace shutter {

const wchar_t* ToneName(int tone) {
    if (tone < 0 || tone >= kToneCount) tone = 0;
    return kRecipes[tone].name;
}

int CurrentTone() {
    const int tone = settings::GetInt(settings::key::kShutterTone, 0);
    // Clamped rather than trusted: this comes out of the registry, where a
    // person with regedit open can put anything at all, and an out-of-range
    // index would read off the end of the table.
    if (tone < 0 || tone >= kToneCount) return 0;
    return tone;
}

void PlayTone(int tone) {
    PlayBuiltIn(tone);
}

} // namespace shutter

} // namespace capture
