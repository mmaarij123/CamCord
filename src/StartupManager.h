#pragma once
#include <windows.h>
#include <string>
#include "RecordingTypes.h"

class StartupManager {
public:
    explicit StartupManager(std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        std::wstring executable = L"");
    bool IsEnabled() const;
    OperationResult SetEnabled(bool enabled) const;
private:
    std::wstring Command() const;
    std::wstring key_, executable_;
};
