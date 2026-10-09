#pragma once
#include <string>
#include <utility>

enum class RecorderState { Idle, Recording, Paused, Saving };

enum class CaptureKind { Display, Window, Region };
struct CaptureTarget {
    CaptureKind kind = CaptureKind::Display;
    unsigned long long handle = 0;
    unsigned long processId = 0, threadId = 0;
    std::wstring windowClass, label = L"Primary display";
    int sourceWidth = 0, sourceHeight = 0;
    int x = 0, y = 0, regionWidth = 0, regionHeight = 0;
};

struct RecordingSettings {
    int width = 1920;
    int height = 1080;
    int fps = 60;
    int bitrateMbps = 0; // 0 keeps resolution/FPS-based automatic quality.
    bool systemAudio = true;
    bool microphone = false;
    bool autoCheckUpdates = true;
    std::wstring outputFolder;
    CaptureTarget captureTarget; // Session-only: never persist/reuse window handles across restarts.
};

struct OperationResult {
    bool ok = false;
    std::wstring message;
    static OperationResult Success(std::wstring text = L"") { return { true, std::move(text) }; }
    static OperationResult Failure(std::wstring text) { return { false, std::move(text) }; }
};
