// ScreenRecorder.h — screen recording to H.264 MP4 via Media Foundation.
//
// The state machine is the interesting part, not the encoding. A recorder
// that can wedge is worse than one that occasionally fails: the failure mode
// is a timer counting up, a Stop button that does nothing, and no way to
// start again short of quitting.
//
// Three rules prevent that:
//
//   * Every recording carries a generation number. Every asynchronous entry
//     point — both watchdogs, the worker's completion message — checks it
//     before acting, so a late callback for a recording that has already been
//     given up on is recognised as stale and discarded.
//   * Watchdogs sit on both the start and the stop path. If the worker never
//     reports, the recording is abandoned and the UI is released.
//   * Because of the generation check, the finish callback fires exactly once
//     per recording. Never twice, never zero times.
//
// All public methods are main-thread only. The worker thread communicates
// solely by posting to the recorder's own message-only window.

#pragma once

#include "framework.h"

class ScreenRecorder {
public:
    // (path, failure) — exactly one is meaningful. A non-empty failure means
    // the recording could not be saved and its message is user-facing copy.
    using FinishCallback = std::function<void(const std::wstring& path,
                                              const std::wstring& failure)>;
    using StateCallback  = std::function<void()>;

    static ScreenRecorder& Shared();

    bool Initialise();
    void Shutdown();

    // `region` is in virtual-desktop coordinates, physical pixels. An empty
    // rectangle records the monitor under the cursor.
    // Returns an empty string on success, or a user-facing failure message.
    std::wstring Start(const RECT& region);
    void         Stop();

    bool         IsRecording() const { return isRecording_; }
    std::wstring ElapsedText() const;   // MM:SS, minutes uncapped

    void SetOnStateChange(StateCallback callback) { onStateChange_ = std::move(callback); }
    void SetOnFinish(FinishCallback callback)     { onFinish_ = std::move(callback); }

    // Stops an in-progress recording and pumps messages until the file has
    // been finalised, for up to three seconds. Finalising is what writes the
    // MP4 index; exiting before it lands leaves an unplayable file.
    void FinishBeforeQuit();

private:
    ScreenRecorder() = default;
    ~ScreenRecorder();
    ScreenRecorder(const ScreenRecorder&) = delete;
    ScreenRecorder& operator=(const ScreenRecorder&) = delete;

    struct WorkerConfig;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI     WorkerEntry(void* parameter);

    void OnWorkerStarted(unsigned long long generation);
    void OnWorkerFinished(unsigned long long generation);
    void OnStartWatchdog(unsigned long long generation);
    void OnStopWatchdog(unsigned long long generation);

    void Abandon(const std::wstring& message);
    void Teardown();
    void JoinWorker();

    HWND               window_       = nullptr;
    ScopedHandle       workerThread_;
    ScopedHandle       stopRequest_;     // manual-reset; set to ask the worker to finish

    bool               isRecording_  = false;
    bool               isStopping_   = false;
    bool               hasStarted_   = false;
    unsigned long long generation_   = 0;
    ULONGLONG          startedAtMs_  = 0;
    bool               mfStarted_    = false;

    // Captured when each watchdog is armed, so the timer's check compares the
    // generation the recording had *then* against the one it has now. Reading
    // the live generation at both ends would make the check vacuous.
    unsigned long long startWatchdogGeneration_ = 0;
    unsigned long long stopWatchdogGeneration_  = 0;

    // Written by the worker, read by the main thread once the completion
    // message has been received — which is a happens-before edge, but the
    // lock costs nothing here and removes the need to argue about it.
    Lock               resultLock_;
    std::wstring       resultPath_;
    std::wstring       resultFailure_;

    StateCallback      onStateChange_;
    FinishCallback     onFinish_;
};
