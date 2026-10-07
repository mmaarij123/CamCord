#pragma once
#include <string>
#include <utility>

enum class RecorderState { Idle, Recording, Paused, Saving };

struct RecordingSettings {
    int width = 1920;
    int height = 1080;
    int fps = 60;
    bool systemAudio = true;
    bool microphone = false;
    std::wstring outputFolder;
};

struct OperationResult {
    bool ok = false;
    std::wstring message;
    static OperationResult Success(std::wstring text = L"") { return { true, std::move(text) }; }
    static OperationResult Failure(std::wstring text) { return { false, std::move(text) }; }
};
