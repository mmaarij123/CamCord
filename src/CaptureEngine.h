#pragma once
#include <string>
#include "Process.h"
#include "RecordingTypes.h"
#include "HardwareEncoderDetector.h"

class CaptureEngine {
public:
    OperationResult Start(const std::wstring& ffmpeg, const std::wstring& output, const std::wstring& log,
        const RecordingSettings& settings, const EncoderChoice& encoder);
    OperationResult Stop();
    bool Running() const { return process_.Running(); }
private:
    ChildProcess process_;
};
