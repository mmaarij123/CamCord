#pragma once
#include "RecordingTypes.h"
#include <array>

inline constexpr std::array<int, 16> BITRATE_PRESETS_MBPS{ 0, 2, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 50, 64, 75, 100 };
inline constexpr int MAX_VIDEO_BITRATE_KBPS = 100000;

inline int NormalizeRecordingHeight(int height) {
    for (const int supported : {480, 720, 1080, 1440, 2160}) if (height == supported) return height;
    return 1080;
}

inline int RecordingWidth(int height) {
    switch (height) {
    case 480: return 854;
    case 720: return 1280;
    case 1440: return 2560;
    case 2160: return 3840;
    default: return 1920;
    }
}

inline int NormalizeRecordingFps(int height, int fps) {
    if (height >= 1440) return 60;
    if (fps == 15 || fps == 30 || fps == 60 || (height == 1080 && fps == 120)) return fps;
    return 60;
}

inline int NormalizeBitrateMbps(int value) {
    for (const int preset : BITRATE_PRESETS_MBPS) if (value == preset) return value;
    return 0;
}

inline int BitrateFromPreference(const std::wstring& text) {
    try {
        size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        return consumed == text.size() ? NormalizeBitrateMbps(value) : 0;
    } catch (...) { return 0; }
}

inline int VideoBitrateKbps(const RecordingSettings& settings) {
    const int manual = NormalizeBitrateMbps(settings.bitrateMbps);
    if (manual) return manual * 1000;
    if (settings.height == 2160) return 64000;
    if (settings.height == 1440) return 32000;
    if (settings.height == 480) return settings.fps >= 60 ? 5000 : (settings.fps <= 15 ? 2500 : 3500);
    if (settings.height == 720) return settings.fps >= 60 ? 8000 : (settings.fps <= 15 ? 4000 : 5500);
    if (settings.fps >= 120) return 24000;
    if (settings.fps >= 60) return 14000;
    return settings.fps <= 15 ? 6000 : 9000;
}

inline int VideoPeakBitrateKbps(const RecordingSettings& settings) {
    const int peak = VideoBitrateKbps(settings) * 13 / 10;
    return peak > MAX_VIDEO_BITRATE_KBPS ? MAX_VIDEO_BITRATE_KBPS : peak;
}
