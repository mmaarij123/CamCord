#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include "RecordingTypes.h"

class AudioCaptureEngine {
public:
    AudioCaptureEngine() = default;
    ~AudioCaptureEngine();
    AudioCaptureEngine(const AudioCaptureEngine&) = delete;
    AudioCaptureEngine& operator=(const AudioCaptureEngine&) = delete;
    OperationResult Start(bool loopback, const std::wstring& wavPath);
    void Stop();
    bool CapturedAudio() const { return capturedAudio_; }
    bool Healthy() const { return !failed_; }
    std::chrono::steady_clock::time_point StartedAt() const { return startedAt_; }
    std::wstring LastError() const;
private:
    void CaptureThread(bool loopback, std::wstring path);
    void SetError(const std::wstring& message);
    std::atomic<bool> stop_{ false };
    std::atomic<bool> started_{ false };
    std::atomic<bool> capturedAudio_{ false };
    std::atomic<bool> finished_{ false }, failed_{ false };
    std::thread thread_;
    mutable std::mutex errorMutex_;
    std::wstring lastError_;
    std::chrono::steady_clock::time_point startedAt_;
};
