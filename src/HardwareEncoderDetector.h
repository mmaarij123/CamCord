#pragma once
#include <string>
#include <vector>
#include "RecordingTypes.h"

struct EncoderChoice { std::wstring name; std::wstring label; bool hardware = false; };

class HardwareEncoderDetector {
public:
    EncoderChoice Detect(const std::wstring& ffmpeg, const std::wstring& workingFolder);
    bool Supports(const std::wstring& ffmpeg, const std::wstring& folder, const EncoderChoice& encoder, const RecordingSettings& settings);
private:
    bool Probe(const std::wstring& ffmpeg, const std::wstring& folder, const std::wstring& encoder, int width, int height, int fps);
};
