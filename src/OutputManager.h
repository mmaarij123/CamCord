#pragma once
#include <string>
#include "RecordingTypes.h"

class OutputManager {
public:
    void SetCaptureFolder(const std::wstring& folder) { captureFolder_ = folder; }
    OperationResult EnsureCaptureFolder();
    OperationResult EnsureFreeSpace(unsigned long long minimumBytes) const;
    std::wstring CaptureFolder() const { return captureFolder_; }
    std::wstring NewOutputPath() const;
    std::wstring NewSessionFolder() const;
private:
    std::wstring captureFolder_;
};
