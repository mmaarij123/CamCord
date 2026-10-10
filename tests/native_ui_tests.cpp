#include "../src/MainWindow.h"
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
struct MainWindowTestAccess {
    static void Run() {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"CamCordIsolatedLifecycleTest";
        Check(RegisterClassW(&type) != 0, "Fixture registration failed");
        {
            MainWindow app;
            // Bypass production WM_CREATE: no WebView, settings file, update
            // request or capture is initialized in these isolated fixtures.
            HWND fixture = CreateWindowW(type.lpszClassName, L"Isolated lifecycle test", WS_OVERLAPPEDWINDOW,
                0, 0, 640, 600, nullptr, nullptr, type.hInstance, nullptr);
            Check(fixture != nullptr, "Fixture creation failed");
            app.hwnd_ = fixture;
            SetWindowLongPtrW(fixture, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
            SetWindowLongPtrW(fixture, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MainWindow::WindowProc));
            app.settings_.captureTarget.kind = CaptureKind::Window;
            app.settings_.captureTarget.handle = 1234;
            app.HandleWebMessage(LR"({"type":"sourceMode","kind":"region"})");
            app.HandleWebMessage(LR"({"type":"pickRegion"})");
            Check(app.settings_.captureTarget.kind == CaptureKind::Window && app.settings_.captureTarget.handle == 1234 &&
                !app.folderDialogOpen_, "Legacy area command changed the selected source or opened a picker");
            MSG picker{};
            Check(!PeekMessageW(&picker, fixture, WM_APP + 4, WM_APP + 4, PM_REMOVE), "Removed area picker was queued");
            app.HandleWebMessage(LR"({"type":"sourceMode","kind":"display"})");
            Check(app.settings_.captureTarget.kind == CaptureKind::Display, "Screen mode switching failed");
            app.HandleWebMessage(LR"({"type":"sourceMode","kind":"window"})");
            Check(app.settings_.captureTarget.kind == CaptureKind::Window && app.settings_.captureTarget.handle == 0,
                "Window mode must require explicit source selection");
            app.folderDialogOpen_ = true;
            Check(!SendMessageW(fixture, WM_QUERYENDSESSION, 0, 0), "Shutdown accepted during modal selection");
            SendMessageW(fixture, WM_CLOSE, 0, 0);
            Check(IsWindow(fixture) && app.closeRequested_, "Close destroyed the owner during a modal selection");
            app.folderDialogOpen_ = false;
            // A queued folder request must be ignored once close/update starts.
            app.ChooseOutputFolder();
            Check(!app.folderDialogOpen_, "Queued folder picker started after close request");
            app.closeRequested_ = false; app.updateStatus_ = L"installing";
            app.ChooseOutputFolder();
            Check(!app.folderDialogOpen_, "Queued folder picker started during installation");
            app.closeRequested_ = true;
            SendMessageW(fixture, WM_CLOSE, 0, 0);
            Check(!IsWindow(fixture), "Close did not complete after modal selection ended");
            MSG quit{}; PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        }
        std::cout << "PASS: removed area commands ignored; Screen/Window modes work; folder modal close/shutdown/install guards preserved\n";
    }
};

int main() {
    try { MainWindowTestAccess::Run(); return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
