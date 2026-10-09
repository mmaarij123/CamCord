#pragma once
#include <string>
#include "Process.h"
#include "RecordingTypes.h"
#include "HardwareEncoderDetector.h"
#include <memory>
#include <chrono>

class CaptureEngine {
public:
    CaptureEngine();
    ~CaptureEngine();
    OperationResult Start(const std::wstring& ffmpeg, const std::wstring& output, const std::wstring& log,
        const RecordingSettings& settings, const EncoderChoice& encoder);
    OperationResult Stop(DWORD timeoutMs = INFINITE);
    bool Running() const;
    std::chrono::steady_clock::time_point StartedAt() const { return startedAt_; }
private:
    struct StreamBridge;
    ChildProcess process_;
    std::unique_ptr<StreamBridge> bridge_;
    std::chrono::steady_clock::time_point startedAt_;
};
