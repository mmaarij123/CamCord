#include "RegionSelector.h"
#include <windowsx.h>
#include <algorithm>

namespace {
struct Selector {
    HWND window = nullptr;
    CaptureTarget target;
    POINT start{}, end{};
    bool dragging = false, done = false, accepted = false;
};
RECT Selection(const Selector& state) {
    return { std::min(state.start.x, state.end.x), std::min(state.start.y, state.end.y),
        std::max(state.start.x, state.end.x), std::max(state.start.y, state.end.y) };
}
LRESULT CALLBACK SelectorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<Selector*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = reinterpret_cast<Selector*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);
    auto position = [&] {
        return POINT{ std::clamp<LONG>(GET_X_LPARAM(lParam), 0, state->target.sourceWidth),
            std::clamp<LONG>(GET_Y_LPARAM(lParam), 0, state->target.sourceHeight) };
    };
    switch (message) {
    case WM_LBUTTONDOWN:
        state->start = state->end = position(); state->dragging = true;
        SetCapture(window); InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE:
        if (state->dragging) { state->end = position(); InvalidateRect(window, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP: {
        if (!state->dragging) return 0;
        state->end = position(); state->dragging = false; ReleaseCapture();
        CaptureTarget selected;
        if (CaptureRegionFromDrag(state->target, state->start.x, state->start.y, state->end.x, state->end.y, selected)) {
            state->target = std::move(selected);
            state->accepted = true; state->done = true;
        } else InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN: if (wParam == VK_ESCAPE) state->done = true; return 0;
    case WM_RBUTTONDOWN: case WM_CLOSE: case WM_DISPLAYCHANGE: state->done = true; return 0;
    case WM_QUERYENDSESSION: state->done = true; return TRUE;
    case WM_CAPTURECHANGED:
        if (state->dragging) { state->dragging = false; InvalidateRect(window, nullptr, FALSE); }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        const HDC memory = CreateCompatibleDC(dc);
        const HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        const auto oldBitmap = SelectObject(memory, bitmap);
        const HBRUSH background = CreateSolidBrush(RGB(12, 15, 20));
        FillRect(memory, &client, background); DeleteObject(background);
        SetBkMode(memory, TRANSPARENT); SetTextColor(memory, RGB(245, 242, 237));
        const UINT dpi = GetDpiForWindow(window);
        const HFONT font = CreateFontW(-MulDiv(20, dpi ? dpi : 96, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        const auto oldFont = SelectObject(memory, font);
        const int margin = MulDiv(24, dpi ? dpi : 96, 96);
        RECT text{ margin, margin, client.right - margin, margin + MulDiv(100, dpi ? dpi : 96, 96) };
        DrawTextW(memory, L"CamCord  \u00b7  Drag to select an area\nEsc or right-click to cancel. Minimum 16 \u00d7 16 pixels.", -1,
            &text, DT_LEFT | DT_WORDBREAK);
        if (state->dragging) {
            const auto rect = Selection(*state);
            const HPEN pen = CreatePen(PS_SOLID, 3, RGB(242, 103, 95));
            const auto oldPen = SelectObject(memory, pen);
            const auto oldBrush = SelectObject(memory, GetStockObject(HOLLOW_BRUSH));
            Rectangle(memory, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(memory, oldBrush); SelectObject(memory, oldPen); DeleteObject(pen);
            const auto size = std::to_wstring(rect.right - rect.left) + L" \u00d7 " + std::to_wstring(rect.bottom - rect.top);
            RECT sizeText{ margin, margin + MulDiv(110, dpi ? dpi : 96, 96), client.right - margin, margin + MulDiv(150, dpi ? dpi : 96, 96) };
            DrawTextW(memory, size.c_str(), -1, &sizeText, DT_LEFT);
        }
        BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, oldFont); DeleteObject(font);
        SelectObject(memory, oldBitmap); DeleteObject(bitmap); DeleteDC(memory);
        EndPaint(window, &paint); return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

bool CaptureRegionFromDrag(const CaptureTarget& display, int x1, int y1, int x2, int y2, CaptureTarget& selected) {
    if (display.sourceWidth <= 0 || display.sourceHeight <= 0) return false;
    x1 = std::clamp(x1, 0, display.sourceWidth); x2 = std::clamp(x2, 0, display.sourceWidth);
    y1 = std::clamp(y1, 0, display.sourceHeight); y2 = std::clamp(y2, 0, display.sourceHeight);
    const int x = std::min(x1, x2), y = std::min(y1, y2), width = std::abs(x1 - x2), height = std::abs(y1 - y2);
    if (!ValidCaptureRegion(display.sourceWidth, display.sourceHeight, x, y, width, height)) return false;
    selected = display; selected.kind = CaptureKind::Region;
    selected.x = x; selected.y = y; selected.regionWidth = width; selected.regionHeight = height;
    return true;
}

bool ChooseCaptureRegion(HWND owner, const CaptureTarget& display, CaptureTarget& selected) {
    auto target = display; target.kind = CaptureKind::Display;
    if (!ResolveCaptureTarget(target).ok) return false;
    MONITORINFO info{ sizeof(info) };
    if (!GetMonitorInfoW(reinterpret_cast<HMONITOR>(target.handle), &info)) return false;
    Selector state; state.target = target; state.target.kind = CaptureKind::Region;
    WNDCLASSEXW type{ sizeof(type) };
    type.lpfnWndProc = SelectorProc; type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"CamCordAreaSelector"; type.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    state.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, type.lpszClassName, L"CamCord area selection",
        WS_POPUP, info.rcMonitor.left, info.rcMonitor.top, target.sourceWidth, target.sourceHeight,
        owner, nullptr, type.hInstance, &state);
    if (!state.window) return false;
    SetLayeredWindowAttributes(state.window, 0, 160, LWA_ALPHA);
    SetWindowDisplayAffinity(state.window, WDA_EXCLUDEFROMCAPTURE);
    const bool wasEnabled = owner && IsWindowEnabled(owner);
    if (wasEnabled) EnableWindow(owner, FALSE);
    ShowWindow(state.window, SW_SHOW); SetForegroundWindow(state.window); SetFocus(state.window);
    MSG message{};
    while (!state.done && IsWindow(state.window)) {
        const int status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) { if (status == 0) PostQuitMessage(static_cast<int>(message.wParam)); break; }
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (GetCapture() == state.window) ReleaseCapture();
    DestroyWindow(state.window);
    if (wasEnabled && IsWindow(owner)) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    if (!state.accepted) return false;
    selected = state.target;
    selected.label = L"Selected area \u00b7 " + std::to_wstring(selected.regionWidth) + L" \u00d7 " + std::to_wstring(selected.regionHeight);
    return true;
}
