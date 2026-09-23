#include "ScreenRecorder.h"

#include "Bitmap.h"
#include "Log.h"
#include "MediaFolder.h"
#include "Util.h"
#include "VideoSettings.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr const wchar_t* kWindowClass = L"SnipTextRecorderSink";

constexpr UINT WM_RECORDER_STARTED  = WM_APP + 1;
constexpr UINT WM_RECORDER_FINISHED = WM_APP + 2;

constexpr UINT_PTR kStartWatchdogTimer = 1;
constexpr UINT_PTR kStopWatchdogTimer  = 2;

// Deliberately generous. Setting up an encoder for a 4K region at 60 fps on a
// busy machine can genuinely take seconds, and the only cost of waiting is a
// later error message — where firing early kills a recording that was about
// to work.
constexpr UINT kWatchdogMs = 10000;

constexpr UINT kQuitBudgetMs = 3000;
constexpr UINT kQuitSliceMs  = 50;

// H.264 with 4:2:0 chroma needs even dimensions in both axes. After a 0.75
// quality scale an odd result is easy to produce, and Media Foundation will
// not round it for you.
int ToEven(int value) { return value & ~1; }

std::wstring ElapsedString(ULONGLONG elapsedMs) {
    const unsigned long long seconds = elapsedMs / 1000;
    return util::Format(L"%02llu:%02llu", seconds / 60, seconds % 60);
}

// Draws the cursor into a captured frame. Windows does not composite it for
// us the way AVFoundation's capturesCursor does.
void DrawCursorInto(HDC dc, const RECT& region) {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!::GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || !info.hCursor) return;

    ICONINFO icon{};
    if (!::GetIconInfo(info.hCursor, &icon)) return;
    // GetIconInfo hands back two bitmaps that belong to the caller.
    ScopedBitmap mask(icon.hbmMask);
    ScopedBitmap colour(icon.hbmColor);

    const int x = info.ptScreenPos.x - region.left - static_cast<int>(icon.xHotspot);
    const int y = info.ptScreenPos.y - region.top  - static_cast<int>(icon.yHotspot);
    ::DrawIconEx(dc, x, y, info.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
}

// A translucent ring around the pointer while a mouse button is down. There
// is no OS-level click highlighting on Windows, so it is drawn by hand.
void DrawClickHighlight(HDC dc, const RECT& region) {
    const bool leftDown  = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool rightDown = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (!leftDown && !rightDown) return;

    POINT cursor{};
    if (!::GetCursorPos(&cursor)) return;

    const int x = cursor.x - region.left;
    const int y = cursor.y - region.top;
    constexpr int radius = 18;

    ScopedPen pen(::CreatePen(PS_SOLID, 3, leftDown ? RGB(255, 210, 0) : RGB(0, 170, 255)));
    if (!pen) return;
    SelectGuard penGuard(dc, pen.get());
    SelectGuard brushGuard(dc, ::GetStockObject(NULL_BRUSH));
    ::Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
}

} // namespace

// ---------------------------------------------------------------------------
// The worker's configuration, copied across the thread boundary once at start
// so the worker never reads mutable recorder state.
// ---------------------------------------------------------------------------

struct ScreenRecorder::WorkerConfig {
    HWND               notify      = nullptr;
    unsigned long long generation  = 0;
    HANDLE             stopRequest = nullptr;
    ScreenRecorder*    owner       = nullptr;

    RECT         region{};
    int          frameRate = 30;
    double       scale     = 1.0;
    bool         drawCursor = true;
    bool         drawClicks = true;
    std::wstring audioDeviceId;
    std::wstring outputPath;
};

namespace {

// --- audio -----------------------------------------------------------------
// Microphone capture is strictly best-effort. A device that has gone away, a
// mix format we cannot convert, an exclusive-mode conflict — none of these
// abort a recording. Losing the picture because the microphone was busy would
// be a much worse outcome than a silent video.

struct AudioStream {
    ComPtr<IAudioClient>        client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX*               mixFormat = nullptr;
    UINT32                      channels   = 0;
    UINT32                      sampleRate = 0;
    bool                        isFloat    = false;
    bool                        active     = false;

    ~AudioStream() { if (mixFormat) ::CoTaskMemFree(mixFormat); }
};

bool OpenAudio(const std::wstring& deviceId, AudioStream& stream) {
    if (deviceId.empty()) return false;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator)))) {
        return false;
    }

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDevice(deviceId.c_str(), &device)) || !device) {
        logging::Write(L"recorder: the selected microphone is gone, recording silently");
        return false;
    }

    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(stream.client.GetAddressOf())))) {
        return false;
    }
    if (FAILED(stream.client->GetMixFormat(&stream.mixFormat)) || !stream.mixFormat) return false;

    const WAVEFORMATEX* format = stream.mixFormat;
    stream.channels   = format->nChannels;
    stream.sampleRate = format->nSamplesPerSec;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        // MFAudioFormat_Float is bit-identical to KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        // and comes from a header this file already includes, which saves
        // dragging in ks.h and ksmedia.h for one comparison.
        stream.isFloat = ::IsEqualGUID(extensible->SubFormat, MFAudioFormat_Float) != 0;
    } else {
        stream.isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    }

    // The AAC encoder takes 16-bit PCM at 44.1 or 48 kHz, mono or stereo.
    // Anything else would need a resampler; skipping audio is the honest
    // answer rather than shipping a half-working conversion.
    const bool usableRate     = stream.sampleRate == 44100 || stream.sampleRate == 48000;
    const bool usableChannels = stream.channels == 1 || stream.channels == 2;
    const bool usableDepth    = stream.isFloat || format->wBitsPerSample == 16;
    if (!usableRate || !usableChannels || !usableDepth) {
        logging::Write(util::Format(L"recorder: microphone format %u Hz / %u ch is unsupported, "
                       L"recording silently",
                       stream.sampleRate, stream.channels));
        return false;
    }

    // A one-second buffer. The writer drains it every frame, so this is pure
    // headroom against a scheduling hiccup.
    constexpr REFERENCE_TIME kBufferDuration = 10000000;
    if (FAILED(stream.client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBufferDuration, 0,
                                         stream.mixFormat, nullptr))) {
        return false;
    }
    if (FAILED(stream.client->GetService(IID_PPV_ARGS(&stream.capture)))) return false;
    if (FAILED(stream.client->Start())) return false;

    stream.active = true;
    return true;
}

// Converts whatever the endpoint gave us into interleaved 16-bit PCM.
void AppendPcm16(const BYTE* source, UINT32 frames, const AudioStream& stream,
                 std::vector<INT16>& out) {
    const UINT32 samples = frames * stream.channels;
    out.resize(samples);
    if (stream.isFloat) {
        const float* input = reinterpret_cast<const float*>(source);
        for (UINT32 i = 0; i < samples; ++i) {
            float value = input[i];
            value = (std::max)(-1.0f, (std::min)(1.0f, value));
            out[i] = static_cast<INT16>(value * 32767.0f);
        }
    } else {
        ::memcpy(out.data(), source, samples * sizeof(INT16));
    }
}

ComPtr<IMFSample> MakeSample(const void* data, size_t bytes, LONGLONG timestamp, LONGLONG duration) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(::MFCreateMemoryBuffer(static_cast<DWORD>(bytes), &buffer)) || !buffer) return nullptr;

    BYTE* target = nullptr;
    if (FAILED(buffer->Lock(&target, nullptr, nullptr)) || !target) return nullptr;
    ::memcpy(target, data, bytes);
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(bytes));

    ComPtr<IMFSample> sample;
    if (FAILED(::MFCreateSample(&sample)) || !sample) return nullptr;
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(timestamp);
    sample->SetSampleDuration(duration);
    return sample;
}

} // namespace

// ---------------------------------------------------------------------------

ScreenRecorder& ScreenRecorder::Shared() {
    static ScreenRecorder recorder;
    return recorder;
}

ScreenRecorder::~ScreenRecorder() = default;

bool ScreenRecorder::Initialise() {
    if (window_ && mfStarted_) return true;
    if (window_) {
        // The window exists but Media Foundation did not start last time;
        // retry just that half rather than reporting success.
        if (FAILED(::MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
        mfStarted_ = true;
        return true;
    }

    WNDCLASSEXW description{};
    description.cbSize        = sizeof(description);
    description.lpfnWndProc   = &ScreenRecorder::WindowProc;
    description.hInstance     = ::GetModuleHandleW(nullptr);
    description.lpszClassName = kWindowClass;
    ::RegisterClassExW(&description);

    // A message-only window. It exists so the worker has somewhere to post
    // to and the watchdogs have something to hang a timer on; it is never
    // shown and never appears anywhere in the UI.
    window_ = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, ::GetModuleHandleW(nullptr), this);
    if (!window_) {
        logging::Write(L"recorder: couldn't create the message window");
        return false;
    }

    if (FAILED(::MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        logging::Write(L"recorder: Media Foundation failed to start");
        return false;
    }
    mfStarted_ = true;
    return true;
}

void ScreenRecorder::Shutdown() {
    JoinWorker();
    if (window_) {
        ::DestroyWindow(window_);
        window_ = nullptr;
    }
    // Only if startup actually succeeded: MFShutdown without a matching
    // MFStartup is an error, and the pair is reference-counted.
    if (mfStarted_) {
        ::MFShutdown();
        mfStarted_ = false;
    }
}

std::wstring ScreenRecorder::ElapsedText() const {
    if (!isRecording_ || startedAtMs_ == 0) return L"00:00";
    return ElapsedString(::GetTickCount64() - startedAtMs_);
}

// --- start -----------------------------------------------------------------

std::wstring ScreenRecorder::Start(const RECT& region) {
    if (isRecording_) return L"A recording is already in progress.";
    // Both halves are checked: Initialise can create the window and then fail
    // at MFStartup, and a window alone is not a working recorder.
    if ((!window_ || !mfStarted_) && !Initialise()) return L"Couldn't start the recording engine.";

    // A previous worker may still be winding down. Joining it here means the
    // new recording never shares a thread or a sink writer with the old one.
    JoinWorker();

    RECT target = region;
    if (util::RectWidth(target) <= 0 || util::RectHeight(target) <= 0) {
        target = util::MonitorBounds(util::MonitorUnderCursor());
    }

    const double scale  = video::CurrentQualityScale();
    const int outWidth  = ToEven(static_cast<int>(util::RectWidth(target) * scale));
    const int outHeight = ToEven(static_cast<int>(util::RectHeight(target) * scale));
    if (outWidth < 16 || outHeight < 16) {
        return L"That region is too small to record.";
    }

    MediaFolder& folder = MediaFolder::Videos();
    if (!folder.EnsureDirectoryExists()) {
        return L"Couldn't create " + folder.DisplayPath() + L".";
    }

    stopRequest_.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stopRequest_) return L"Couldn't prepare the recording.";

    auto config = std::make_unique<WorkerConfig>();
    config->notify        = window_;
    config->generation    = ++generation_;
    config->stopRequest   = stopRequest_.get();
    config->owner         = this;
    config->region        = target;
    config->frameRate     = video::FrameRate();
    config->scale         = scale;
    config->drawCursor    = video::CapturesCursor();
    config->drawClicks    = video::CapturesClicks();
    config->audioDeviceId = video::AudioDeviceId();
    config->outputPath    = folder.NewFilePath();

    {
        LockGuard guard(resultLock_);
        resultPath_.clear();
        resultFailure_.clear();
    }

    // State flips before the thread starts, so the UI reflects the intent
    // immediately. The start watchdog is the only thing that corrects a
    // recording that never actually begins.
    isRecording_ = true;
    isStopping_  = false;
    hasStarted_  = false;
    startedAtMs_ = ::GetTickCount64();

    const std::wstring path      = config->outputPath;
    const int          frameRate = config->frameRate;
    WorkerConfig* raw = config.release();   // the worker takes ownership
    workerThread_.reset(::CreateThread(nullptr, 0, &ScreenRecorder::WorkerEntry, raw, 0, nullptr));
    if (!workerThread_) {
        delete raw;
        isRecording_ = false;
        startedAtMs_ = 0;
        return L"Couldn't start the recording thread.";
    }

    startWatchdogGeneration_ = generation_;
    ::SetTimer(window_, kStartWatchdogTimer, kWatchdogMs, nullptr);

    logging::Write(util::Format(L"recorder: started %dx%d @%dfps %s → %s",
                   outWidth, outHeight, frameRate,
                   video::QualityShortTitle(video::CurrentQuality()),
                   util::LastPathComponent(path).c_str()));

    if (onStateChange_) onStateChange_();
    return std::wstring();
}

// --- stop ------------------------------------------------------------------

void ScreenRecorder::Stop() {
    // Three separate affordances route here — the menu item, the tray icon
    // and the hotkey — so without the guard a second Stop would signal a
    // worker that has already finished.
    if (!isRecording_ || isStopping_) return;

    isStopping_ = true;
    logging::Write(L"recorder: stopping after " + ElapsedText());

    if (stopRequest_) ::SetEvent(stopRequest_.get());
    stopWatchdogGeneration_ = generation_;
    if (window_) ::SetTimer(window_, kStopWatchdogTimer, kWatchdogMs, nullptr);
}

void ScreenRecorder::FinishBeforeQuit() {
    if (!isRecording_) return;

    logging::Write(L"quit: finishing the in-progress recording first");
    Stop();

    // Pump messages rather than blocking: the worker's completion arrives as
    // a posted message, so a plain wait here would deadlock against the very
    // thing it is waiting for.
    // Restricted to this recorder's own window. Pumping the whole thread
    // queue here would dispatch anything pending — re-entering the menu, an
    // editor's window procedure, or swallowing a WM_QUIT that the app's loop
    // still needs to see.
    const ULONGLONG deadline = ::GetTickCount64() + kQuitBudgetMs;
    while (isRecording_ && ::GetTickCount64() < deadline) {
        MSG message{};
        while (::PeekMessageW(&message, window_, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (!isRecording_) break;
        ::Sleep(kQuitSliceMs);
    }
    JoinWorker();
}

// --- callbacks -------------------------------------------------------------

LRESULT CALLBACK ScreenRecorder::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ScreenRecorder* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ScreenRecorder*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ScreenRecorder*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_RECORDER_STARTED:
        self->OnWorkerStarted(static_cast<unsigned long long>(wParam));
        return 0;
    case WM_RECORDER_FINISHED:
        self->OnWorkerFinished(static_cast<unsigned long long>(wParam));
        return 0;
    case WM_TIMER:
        if (wParam == kStartWatchdogTimer) {
            ::KillTimer(hwnd, kStartWatchdogTimer);
            self->OnStartWatchdog(self->startWatchdogGeneration_);
        } else if (wParam == kStopWatchdogTimer) {
            ::KillTimer(hwnd, kStopWatchdogTimer);
            self->OnStopWatchdog(self->stopWatchdogGeneration_);
        }
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void ScreenRecorder::OnWorkerStarted(unsigned long long generation) {
    if (!isRecording_ || generation != generation_) return;   // stale
    hasStarted_ = true;
    if (window_) ::KillTimer(window_, kStartWatchdogTimer);
    LOG_DEBUG(L"recorder: capture confirmed started");
}

void ScreenRecorder::OnWorkerFinished(unsigned long long generation) {
    if (generation != generation_ || !isRecording_) {
        LOG_DEBUG(L"recorder: ignoring a late callback");
        return;
    }

    std::wstring path, failure;
    {
        LockGuard guard(resultLock_);
        path    = resultPath_;
        failure = resultFailure_;
    }

    Teardown();

    // The state callback runs first, because it is what clears the recording
    // indicator. Firing it afterwards would wipe the "saved" confirmation in
    // the same turn.
    if (onStateChange_) onStateChange_();

    if (!failure.empty()) {
        logging::Write(L"recorder: FAILED — " + failure);
        if (onFinish_) onFinish_(std::wstring(), failure);
    } else if (!path.empty()) {
        logging::Write(L"recorder: saved " + util::LastPathComponent(path));
        if (onFinish_) onFinish_(path, std::wstring());
    }
}

void ScreenRecorder::OnStartWatchdog(unsigned long long generation) {
    if (!isRecording_ || generation != generation_ || hasStarted_) return;
    logging::Write(L"recorder: never started, giving up");
    Abandon(L"The recording never started. Nothing was written.");
}

void ScreenRecorder::OnStopWatchdog(unsigned long long generation) {
    if (!isRecording_ || generation != generation_) return;
    logging::Write(L"recorder: stop never completed");
    Abandon(L"The recording didn't finish cleanly. Any file that was written is in "
            + MediaFolder::Videos().DisplayPath() + L".");
}

void ScreenRecorder::Abandon(const std::wstring& message) {
    // Ask the worker to wind down first so it can close the file properly,
    // then tear state down. Because Teardown bumps the generation, whatever
    // the worker eventually posts is stale and will be discarded.
    if (stopRequest_) ::SetEvent(stopRequest_.get());

    Teardown();
    if (onStateChange_) onStateChange_();
    if (onFinish_) onFinish_(std::wstring(), message);
}

void ScreenRecorder::Teardown() {
    if (window_) {
        ::KillTimer(window_, kStartWatchdogTimer);
        ::KillTimer(window_, kStopWatchdogTimer);
    }
    // Bumping the generation is what makes every outstanding callback stale.
    ++generation_;
    isRecording_ = false;
    isStopping_  = false;
    hasStarted_  = false;
    startedAtMs_ = 0;
}

void ScreenRecorder::JoinWorker() {
    if (!workerThread_) return;
    if (stopRequest_) ::SetEvent(stopRequest_.get());

    if (::WaitForSingleObject(workerThread_.get(), 5000) != WAIT_OBJECT_0) {
        // The worker is still running and still waiting on the stop event.
        // Closing these now would let the next recording's CreateEvent reuse
        // the same handle value, and the abandoned worker would then be
        // sharing an event with a live recording. Leaking two handles from a
        // path that should never be taken is the cheaper mistake.
        logging::Write(L"recorder: worker didn't finish in time, releasing it");
        workerThread_.release();
        stopRequest_.release();
        return;
    }

    workerThread_.reset();
    stopRequest_.reset();
}

// ---------------------------------------------------------------------------
// The worker. Owns every Media Foundation object for one recording and
// nothing else; it never touches recorder state except through the two
// posted messages and the result lock.
// ---------------------------------------------------------------------------

DWORD WINAPI ScreenRecorder::WorkerEntry(void* parameter) {
    std::unique_ptr<WorkerConfig> config(static_cast<WorkerConfig*>(parameter));
    ScreenRecorder* owner = config->owner;

    auto report = [&](const std::wstring& path, const std::wstring& failure) {
        {
            LockGuard guard(owner->resultLock_);
            owner->resultPath_    = path;
            owner->resultFailure_ = failure;
        }
        ::PostMessageW(config->notify, WM_RECORDER_FINISHED,
                       static_cast<WPARAM>(config->generation), 0);
    };

    if (FAILED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        report(std::wstring(), L"Couldn't start the recording thread.");
        return 0;
    }
    // Declared before the block below so it is destroyed last. Every COM
    // pointer in that block must release into a live apartment; calling
    // CoUninitialize on an early-exit path while those pointers were still in
    // scope would release them into a dead one.
    struct ComScope {
        ~ComScope() { ::CoUninitialize(); }
    } comScope;

    {
        const int sourceWidth  = util::RectWidth(config->region);
        const int sourceHeight = util::RectHeight(config->region);
        const int width  = ToEven(static_cast<int>(sourceWidth * config->scale));
        const int height = ToEven(static_cast<int>(sourceHeight * config->scale));

        // A bitrate proportional to the pixel rate. Hand-picked numbers get
        // subtly wrong at the extremes; this stays sane from a small snip at
        // 15 fps to a 4K screen at 60.
        const double pixelRate = static_cast<double>(width) * height * config->frameRate;
        UINT32 bitrate = static_cast<UINT32>((std::min)(40000000.0,
                                                        (std::max)(1500000.0, pixelRate * 0.12)));

        ComPtr<IMFSinkWriter> writer;
        DWORD videoStream = 0;
        DWORD audioStream = 0;
        bool  hasAudio    = false;
        AudioStream audio;

        ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(::MFCreateAttributes(&attributes, 2))) {
            attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        }

        HRESULT hr = ::MFCreateSinkWriterFromURL(config->outputPath.c_str(), nullptr,
                                                 attributes.Get(), &writer);
        if (FAILED(hr) || !writer) {
            report(std::wstring(), L"Couldn't create the movie file.");
            return 0;
        }

        // --- video stream ---
        {
            ComPtr<IMFMediaType> outputType;
            ::MFCreateMediaType(&outputType);
            outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
            outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
            outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            ::MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height);
            ::MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, config->frameRate, 1);
            ::MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

            if (FAILED(writer->AddStream(outputType.Get(), &videoStream))) {
                report(std::wstring(), L"This machine has no H.264 encoder available.");
                return 0;
            }

            ComPtr<IMFMediaType> inputType;
            ::MFCreateMediaType(&inputType);
            inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            // A positive stride means top-down rows, which is how our DIB
            // sections are laid out. Leaving it unset gets a bottom-up
            // interpretation and a vertically mirrored recording.
            inputType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4));
            ::MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height);
            ::MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, config->frameRate, 1);
            ::MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

            if (FAILED(writer->SetInputMediaType(videoStream, inputType.Get(), nullptr))) {
                report(std::wstring(), L"The encoder refused the capture format.");
                return 0;
            }
        }

        // --- audio stream, best effort only ---
        if (OpenAudio(config->audioDeviceId, audio)) {
            ComPtr<IMFMediaType> outputType;
            ::MFCreateMediaType(&outputType);
            outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            outputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            outputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio.sampleRate);
            outputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio.channels);
            outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);

            ComPtr<IMFMediaType> inputType;
            ::MFCreateMediaType(&inputType);
            inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            inputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            inputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            inputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio.sampleRate);
            inputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio.channels);
            inputType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audio.channels * 2);
            inputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                 audio.sampleRate * audio.channels * 2);

            if (SUCCEEDED(writer->AddStream(outputType.Get(), &audioStream)) &&
                SUCCEEDED(writer->SetInputMediaType(audioStream, inputType.Get(), nullptr))) {
                hasAudio = true;
                logging::Write(L"recorder: recording audio from the selected microphone");
            } else {
                logging::Write(L"recorder: the AAC encoder refused the microphone, recording silently");
            }
        }

        if (FAILED(writer->BeginWriting())) {
            report(std::wstring(), L"The movie writer refused to start.");
            return 0;
        }

        // The worker is live. Tell the main thread so it can disarm the start
        // watchdog.
        ::PostMessageW(config->notify, WM_RECORDER_STARTED,
                       static_cast<WPARAM>(config->generation), 0);

        // --- the capture loop ---
        auto frame  = Bitmap::Create(sourceWidth, sourceHeight);
        auto scaled = (width == sourceWidth && height == sourceHeight)
                          ? nullptr : Bitmap::Create(width, height);
        if (!frame || ((width != sourceWidth || height != sourceHeight) && !scaled)) {
            report(std::wstring(), L"Couldn't allocate the capture buffers.");
            return 0;
        }

        const LONGLONG frameDuration = 10000000LL / config->frameRate;
        const ULONGLONG startTick    = ::GetTickCount64();
        LONGLONG videoTimestamp      = 0;
        LONGLONG audioTimestamp      = 0;
        long long frameIndex         = 0;
        std::wstring failure;
        std::vector<INT16> pcm;

        for (;;) {
            if (::WaitForSingleObject(config->stopRequest, 0) == WAIT_OBJECT_0) break;

            // --- one video frame ---
            {
                HDC target = frame->MemoryDC();
                WindowDC screen(nullptr);
                if (target && screen) {
                    ::BitBlt(target, 0, 0, sourceWidth, sourceHeight, screen.get(),
                             config->region.left, config->region.top, SRCCOPY | CAPTUREBLT);
                    if (config->drawCursor) DrawCursorInto(target, config->region);
                    if (config->drawClicks) DrawClickHighlight(target, config->region);
                    frame->MakeOpaque();
                }

                const Bitmap* source = frame.get();
                if (scaled && scaled->MemoryDC() && frame->MemoryDC()) {
                    ::SetStretchBltMode(scaled->MemoryDC(), HALFTONE);
                    ::SetBrushOrgEx(scaled->MemoryDC(), 0, 0, nullptr);
                    ::StretchBlt(scaled->MemoryDC(), 0, 0, width, height,
                                 frame->MemoryDC(), 0, 0, sourceWidth, sourceHeight, SRCCOPY);
                    source = scaled.get();
                }

                // The byte count comes from the bitmap actually being sent,
                // not from the configured output size. If the scale step
                // silently fell back to the unscaled frame, describing its
                // rows with the scaled stride would produce a sheared
                // recording rather than a clean failure.
                if (source->Width() != width || source->Height() != height) {
                    failure = L"The capture buffer didn't match the encoder's frame size.";
                    break;
                }
                ComPtr<IMFSample> sample =
                    MakeSample(source->Bits(),
                               static_cast<size_t>(source->Stride()) * source->Height(),
                               videoTimestamp, frameDuration);
                if (sample) {
                    HRESULT written = writer->WriteSample(videoStream, sample.Get());
                    if (FAILED(written)) {
                        failure = L"The encoder stopped accepting frames.";
                        break;
                    }
                }
                videoTimestamp += frameDuration;
            }

            // --- whatever audio has arrived since the last frame ---
            if (hasAudio && audio.capture) {
                UINT32 available = 0;
                while (SUCCEEDED(audio.capture->GetNextPacketSize(&available)) && available > 0) {
                    BYTE*  data  = nullptr;
                    UINT32 count = 0;
                    DWORD  flags = 0;
                    if (FAILED(audio.capture->GetBuffer(&data, &count, &flags, nullptr, nullptr))) {
                        break;
                    }
                    if (count > 0 && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                        AppendPcm16(data, count, audio, pcm);
                        const LONGLONG duration =
                            static_cast<LONGLONG>(count) * 10000000LL / audio.sampleRate;
                        ComPtr<IMFSample> sample =
                            MakeSample(pcm.data(), pcm.size() * sizeof(INT16),
                                       audioTimestamp, duration);
                        if (sample) writer->WriteSample(audioStream, sample.Get());
                        audioTimestamp += duration;
                    }
                    audio.capture->ReleaseBuffer(count);
                }
            }

            // Pace against wall-clock time rather than sleeping a fixed
            // interval, so a slow frame does not make the whole recording
            // drift behind real time.
            ++frameIndex;
            const ULONGLONG due = startTick + static_cast<ULONGLONG>(frameIndex * 1000 / config->frameRate);
            const ULONGLONG now = ::GetTickCount64();
            if (due > now) {
                ::WaitForSingleObject(config->stopRequest, static_cast<DWORD>(due - now));
            }
        }

        if (audio.active && audio.client) audio.client->Stop();

        // Finalize writes the MP4 index. Skipping it leaves a file no player
        // will open, which is why the quit path waits for this.
        HRESULT finalized = writer->Finalize();
        writer.Reset();

        if (!failure.empty()) {
            report(std::wstring(), failure);
        } else if (FAILED(finalized)) {
            report(std::wstring(), L"The movie file couldn't be finished.");
        } else if (frameIndex == 0) {
            // A zero-frame file is unplayable and would inflate the saved
            // count; remove it rather than leaving it behind.
            ::DeleteFileW(config->outputPath.c_str());
            report(std::wstring(), L"No frames were captured.");
        } else {
            report(config->outputPath, std::wstring());
        }
    }

    return 0;
}
