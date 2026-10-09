#pragma once
#include "RecordingTypes.h"
#include <array>

inline constexpr std::array<int, 16> BITRATE_PRESETS_MBPS{ 0, 2, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 50, 64, 75, 100 };
inline constexpr int MAX_VIDEO_BITRATE_KBPS = 100000;

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
