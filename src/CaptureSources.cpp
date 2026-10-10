#include "CaptureSources.h"
#include <dwmapi.h>
#include <algorithm>

namespace {
struct Enumeration { std::vector<CaptureSourceEntry> sources; DWORD excludedProcess; };
BOOL CALLBACK MonitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto& state = *reinterpret_cast<Enumeration*>(data);
    MONITORINFO info{ sizeof(info) };
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    CaptureTarget target;
    target.handle = reinterpret_cast<unsigned long long>(monitor);
    target.sourceWidth = info.rcMonitor.right - info.rcMonitor.left;
    target.sourceHeight = info.rcMonitor.bottom - info.rcMonitor.top;
    const bool primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    target.label = primary ? L"Primary display" : L"Display " + std::to_wstring(state.sources.size() + 1);
    target.label += L" \u00b7 " + std::to_wstring(target.sourceWidth) + L" \u00d7 " + std::to_wstring(target.sourceHeight);
    state.sources.push_back({ CaptureSourceId(target), target, primary });
    return TRUE;
}
BOOL CALLBACK WindowCallback(HWND window, LPARAM data) {
    auto& state = *reinterpret_cast<Enumeration*>(data);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == state.excludedProcess || !IsWindowVisible(window) || IsIconic(window) ||
        window == GetShellWindow() || GetAncestor(window, GA_ROOT) != window ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    const auto target = CaptureWindowTarget(window);
    if (target.label.empty() || target.windowClass == L"Shell_TrayWnd" || target.windowClass == L"Shell_SecondaryTrayWnd" ||
        target.sourceWidth < 16 || target.sourceHeight < 16) return TRUE;
    state.sources.push_back({ CaptureSourceId(target), target, false });
    return TRUE;
}
}

CaptureTarget CaptureWindowTarget(HWND window) {
    CaptureTarget target;
    target.kind = CaptureKind::Window;
    target.handle = reinterpret_cast<unsigned long long>(window);
    target.threadId = GetWindowThreadProcessId(window, &target.processId);
    wchar_t title[1024]{}, className[256]{};
    GetWindowTextW(window, title, 1024);
    GetClassNameW(window, className, 256);
    target.label = title; target.windowClass = className;
    RECT rect{};
    if (GetClientRect(window, &rect)) { target.sourceWidth = rect.right; target.sourceHeight = rect.bottom; }
    return target;
}

std::wstring CaptureSourceId(const CaptureTarget& target) {
    return (target.kind == CaptureKind::Window ? L"window:" : L"display:") + std::to_wstring(target.handle)
        + (target.kind == CaptureKind::Window ? L":" + std::to_wstring(target.processId) + L":" + std::to_wstring(target.threadId) : L"");
}

std::vector<CaptureSourceEntry> EnumerateCaptureSources(DWORD excludedProcess) {
    Enumeration state{ {}, excludedProcess };
    EnumDisplayMonitors(nullptr, nullptr, MonitorCallback, reinterpret_cast<LPARAM>(&state));
    std::stable_sort(state.sources.begin(), state.sources.end(), [](const auto& a, const auto& b) { return a.primary && !b.primary; });
    EnumWindows(WindowCallback, reinterpret_cast<LPARAM>(&state));
    return state.sources;
}

CaptureTarget PrimaryCaptureDisplay() {
    Enumeration state{ {}, GetCurrentProcessId() };
    EnumDisplayMonitors(nullptr, nullptr, MonitorCallback, reinterpret_cast<LPARAM>(&state));
    for (const auto& source : state.sources) if (source.primary) return source.target;
    return state.sources.empty() ? CaptureTarget{} : state.sources.front().target;
}

bool CaptureSourceExists(const CaptureTarget& target) {
    if (!target.handle || (target.kind != CaptureKind::Display && target.kind != CaptureKind::Window)) return false;
    if (target.kind != CaptureKind::Window) {
        MONITORINFO info{ sizeof(info) };
        if (!GetMonitorInfoW(reinterpret_cast<HMONITOR>(target.handle), &info)) return false;
        return true;
    }
    const HWND window = reinterpret_cast<HWND>(target.handle);
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    wchar_t className[256]{};
    GetClassNameW(window, className, 256);
    return IsWindow(window) && process == target.processId && thread == target.threadId && target.windowClass == className;
}

OperationResult ResolveCaptureTarget(CaptureTarget& target) {
    if (target.kind == CaptureKind::Display && !target.handle) target = PrimaryCaptureDisplay();
    if (!CaptureSourceExists(target)) return OperationResult::Failure(L"The selected source is unavailable. Choose the display or window again.");
    if (target.kind == CaptureKind::Window) {
        const HWND window = reinterpret_cast<HWND>(target.handle);
        if (IsIconic(window) || !IsWindowVisible(window)) return OperationResult::Failure(L"Restore the selected window before recording.");
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
            return OperationResult::Failure(L"The selected window is on another desktop. Choose a visible window.");
        const auto current = CaptureWindowTarget(window);
        if (current.sourceWidth < 16 || current.sourceHeight < 16) return OperationResult::Failure(L"The selected window is too small to record.");
        target.sourceWidth = current.sourceWidth; target.sourceHeight = current.sourceHeight;
    } else {
        MONITORINFO info{ sizeof(info) };
        GetMonitorInfoW(reinterpret_cast<HMONITOR>(target.handle), &info);
        target.sourceWidth = info.rcMonitor.right - info.rcMonitor.left;
        target.sourceHeight = info.rcMonitor.bottom - info.rcMonitor.top;
    }
    return OperationResult::Success();
}

std::wstring CaptureInputFilter(const CaptureTarget& target, int fps) {
    std::wstring filter = L"gfxcapture=";
    filter += target.kind == CaptureKind::Window ? L"hwnd=" : L"hmonitor=";
    filter += std::to_wstring(target.handle) + L":capture_cursor=0:output_fmt=bgra:max_framerate=" + std::to_wstring(fps);
    if (target.kind == CaptureKind::Window) filter += L":capture_border=0:resize_mode=scale_aspect";
    return filter;
}
