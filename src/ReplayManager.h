#pragma once
#include "CaptureEngine.h"
#include "AudioCaptureEngine.h"
#include "OutputManager.h"
#include "ReplaySettings.h"
#include <atomic>
#include <mutex>
#include <thread>
class ReplayManager {
public:
    ~ReplayManager();
    OperationResult Start(RecordingSettings& settings, const std::wstring& folder);
    OperationResult Stop(bool discard = true);
    OperationResult SaveClip(double endSeconds);
    double Elapsed() const;
    int Buffered() const { return buffered_; }
    bool Healthy() const { return healthy_; }
    std::wstring LastOutput() const { return lastOutput_; } // worker ownership, after completion only
private:
    friend struct ReplayTestAccess;
    void Maintain();
    void Prune();
    RecordingSettings settings_;
    EncoderChoice encoder_;
    OutputManager output_;
    CaptureEngine capture_;
    AudioCaptureEngine system_, mic_;
    std::wstring ffmpeg_, session_, lastOutput_;
    std::chrono::steady_clock::time_point began_;
    double systemOffset_ = 0, micOffset_ = 0;
    std::atomic<bool> stopping_{true}, healthy_{false};
    std::atomic<int> buffered_{0};
    std::thread maintenance_;
    std::mutex files_;
    bool retain_ = false;
};
