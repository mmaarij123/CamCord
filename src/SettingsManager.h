#pragma once
#include "RecordingTypes.h"
#include <utility>

class SettingsManager {
public:
    explicit SettingsManager(std::wstring path = {}) : pathOverride_(std::move(path)) {}
    RecordingSettings Load() const;
    OperationResult Save(const RecordingSettings& settings) const;
private:
    std::wstring pathOverride_;
    std::wstring SettingsPath() const;
};
