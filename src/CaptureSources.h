#pragma once
#include <windows.h>
#include <vector>
#include "RecordingTypes.h"

struct CaptureSourceEntry {
    std::wstring id;
    CaptureTarget target;
    bool primary = false;
};
std::vector<CaptureSourceEntry> EnumerateCaptureSources(DWORD excludedProcess = GetCurrentProcessId());
CaptureTarget PrimaryCaptureDisplay();
CaptureTarget CaptureWindowTarget(HWND window);
std::wstring CaptureSourceId(const CaptureTarget& target);
bool CaptureSourceExists(const CaptureTarget& target);
OperationResult ResolveCaptureTarget(CaptureTarget& target);
std::wstring CaptureInputFilter(const CaptureTarget& target, int fps);
