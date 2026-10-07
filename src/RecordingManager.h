#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "AudioCaptureEngine.h"
#include "CaptureEngine.h"
#include "HardwareEncoderDetector.h"
#include "OutputManager.h"
#include "RecordingTypes.h"

class RecordingManager {
public:
    RecordingManager();
    OperationResult Initialize();
    OperationResult Start(RecordingSettings& settings);
    OperationResult Pause();
    OperationResult Resume();
    OperationResult Stop();
    void SetOutputFolder(const std::wstring& folder) { output_.SetCaptureFolder(folder); }
    std::wstring CaptureFolder() const { return output_.CaptureFolder(); }
    RecorderState State() const { return state_; }
    unsigned long long ElapsedSeconds() const;
    bool CaptureAlive() const;
    std::wstring LastOutput() const { return lastOutput_; }
    std::wstring EncoderLabel() const { return encoder_.label; }
private:
    friend struct PipelineTestAccess;
    struct Segment {
        std::wstring video, systemAudio, microphone, muxed, log;
        bool hasSystem = false, hasMic = false;
        long long systemTrimMs = 0, micTrimMs = 0;
    };
    OperationResult StartSegment();
    OperationResult StopSegment();
    OperationResult Finalize();
    OperationResult RunFfmpeg(const std::wstring& args, const std::wstring& log, DWORD timeout = INFINITE);
    OperationResult Failure(const std::wstring& message) const;
    void Cleanup();
    RecordingSettings settings_;
    RecorderState state_ = RecorderState::Idle;
    OutputManager output_;
    HardwareEncoderDetector detector_;
    EncoderChoice encoder_;
    std::wstring ffmpeg_, sessionFolder_, outputPath_, lastOutput_, segmentFailure_;
    std::vector<Segment> segments_;
    std::unique_ptr<CaptureEngine> capture_;
    std::unique_ptr<AudioCaptureEngine> systemAudio_, microphone_;
    std::chrono::steady_clock::time_point segmentStarted_;
    std::chrono::milliseconds accumulated_{ 0 };
};
