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
        for (const bool region : {false, true}) {
            MainWindow app;
            // Bypass production WM_CREATE: no WebView, settings file, update
            // request or capture is initialized in these isolated fixtures.
            HWND fixture = CreateWindowW(type.lpszClassName, L"Isolated lifecycle test", WS_OVERLAPPEDWINDOW,
                0, 0, 640, 600, nullptr, nullptr, type.hInstance, nullptr);
            Check(fixture != nullptr, "Fixture creation failed");
            app.hwnd_ = fixture;
            SetWindowLongPtrW(fixture, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
            SetWindowLongPtrW(fixture, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MainWindow::WindowProc));
            app.folderDialogOpen_ = true; app.selectingSource_ = region;
            Check(!SendMessageW(fixture, WM_QUERYENDSESSION, 0, 0), "Shutdown accepted during modal selection");
            SendMessageW(fixture, WM_CLOSE, 0, 0);
            Check(IsWindow(fixture) && app.closeRequested_, "Close destroyed the owner during a modal selection");
            app.folderDialogOpen_ = false; app.selectingSource_ = false;
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
        std::cout << "PASS: folder/area modal owner survives close; shutdown blocked; close completes; queued picker guarded during close/install\n";
    }
};

int main() {
    try { MainWindowTestAccess::Run(); return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
