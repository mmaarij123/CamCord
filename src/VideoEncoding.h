#pragma once
#include "RecordingQuality.h"
#include "HardwareEncoderDetector.h"
#include <string>

// Shared by desktop capture and synthetic encoder tests, so presets exercise
// the same flags that real recordings use.
inline std::wstring VideoEncodingArguments(const RecordingSettings& settings, const EncoderChoice& encoder) {
    std::wstring arguments = L" -an -c:v " + encoder.name;
    if (encoder.name == L"h264_nvenc") arguments += L" -preset p4 -tune hq -rc vbr";
    else if (encoder.name == L"h264_amf") arguments += L" -quality balanced -rc vbr_peak";
    else if (encoder.name == L"h264_qsv") arguments += L" -preset medium";
    else arguments += L" -preset veryfast -tune zerolatency";
    arguments += L" -b:v " + std::to_wstring(VideoBitrateKbps(settings)) + L"k -maxrate "
        + std::to_wstring(VideoPeakBitrateKbps(settings)) + L"k -bufsize "
        + std::to_wstring(VideoBitrateKbps(settings) * 2) + L"k -g " + std::to_wstring(settings.fps * 2)
        + L" -pix_fmt yuv420p -r " + std::to_wstring(settings.fps);
    return arguments;
}
