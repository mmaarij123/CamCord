#include "../src/MainWindow.h"
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
struct ReplayTestAccess {
    // No capture process is created. Simulate a still-live cached producer after
    // its selected source disappeared, to exercise the independent UI-thread guard.
    static void CachedProducer(ReplayManager& replay) { replay.healthy_=true; }
};
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
            // Only isolated CI desktop sessions touch global registrations.
            wchar_t ci[8]{};
            if(GetEnvironmentVariableW(L"CI",ci,8)&&std::wstring(ci)==L"true") {
                Check(RegisterHotKey(fixture,2708,ReplayModifiers(L"Ctrl+Shift+F8"),ReplayKey(L"Ctrl+Shift+F8"))!=FALSE,"Windows global clip shortcut registration failed");
                Check(UnregisterHotKey(fixture,2708)!=FALSE,"Windows shortcut cleanup failed");
                Check(RegisterHotKey(fixture,2708,ReplayModifiers(L"Ctrl+Alt+F10"),ReplayKey(L"Ctrl+Alt+F10"))!=FALSE,"Alternate Windows clip shortcut failed");
                Check(UnregisterHotKey(fixture,2708)!=FALSE,"Alternate shortcut cleanup failed");
                std::cout<<"PASS: Windows CI global shortcut registration and cleanup\n";
            }
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
            SendMessageW(fixture, WM_HOTKEY, 1708, 0);
            Check(app.replayPending_.empty(), "Hotkey started capture with replay off");
            app.replayEnabled_ = true;
            app.ChooseOutputFolder();Check(!app.folderDialogOpen_, "Folder changed during replay capture");
            app.HandleWebMessage(LR"({"type":"sourceMode","kind":"display"})");
            Check(app.settings_.captureTarget.kind == CaptureKind::Window, "Source changed while replay was capturing");
            app.updateStatus_=L"ready";app.BeginUpdate(MainWindow::UpdateAction::Verify,true);
            Check(app.updateAction_==MainWindow::UpdateAction::None, "Update install started with replay on");
            app.updateStatus_=L"idle";app.replayEnabled_=false;
            app.replayEnabled_=true;ReplayTestAccess::CachedProducer(app.replay_);
            app.PollReplay();
            Check(app.replayPending_==L"disable", "Lost replay source did not stop a cached/live producer");
            for(int index=0;index<100&&!app.replayPending_.empty();index++){Sleep(10);app.PollReplay();}
            Check(!app.replayEnabled_&&app.replayPending_.empty(), "Source-loss replay shutdown did not finish");
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
