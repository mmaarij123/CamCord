#include "HardwareEncoderDetector.h"
#include "Process.h"
#include <filesystem>

bool HardwareEncoderDetector::Probe(const std::wstring& ffmpeg, const std::wstring& folder, const std::wstring& encoder, int width, int height, int fps) {
    const auto log = (std::filesystem::path(folder) / (L"probe_" + encoder + L".log")).wstring();
    std::wstring args = L"-hide_banner -loglevel error -f lavfi -i " + QuoteArg(L"color=c=black:s=" + std::to_wstring(width) + L"x" + std::to_wstring(height) + L":r=" + std::to_wstring(fps))
        + L" -frames:v 8 -an -c:v " + encoder;
    if (encoder == L"h264_nvenc") args += L" -preset p4";
    if (encoder == L"libx264") args += L" -preset veryfast";
    args += L" -f null NUL";
    return ChildProcess::Run(ffmpeg, args, log, 20000) == 0;
}

EncoderChoice HardwareEncoderDetector::Detect(const std::wstring& ffmpeg, const std::wstring& folder) {
    const std::vector<EncoderChoice> choices = {
        {L"h264_nvenc", L"NVIDIA NVENC", true}, {L"h264_amf", L"AMD AMF", true}, {L"h264_qsv", L"Intel Quick Sync", true}
    };
    for (const auto& c : choices) if (Probe(ffmpeg, folder, c.name, 640, 360, 30)) return c;
    if (Probe(ffmpeg, folder, L"libx264", 640, 360, 30)) return { L"libx264", L"Software H.264", false };
    return {};
}

bool HardwareEncoderDetector::Supports(const std::wstring& ffmpeg, const std::wstring& folder, const EncoderChoice& encoder, const RecordingSettings& settings) {
    return !encoder.name.empty() && Probe(ffmpeg, folder, encoder.name, settings.width, settings.height, settings.fps);
}
