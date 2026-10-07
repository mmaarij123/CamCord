#include "MainWindow.h"
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    HANDLE singleInstance = CreateMutexW(nullptr, FALSE, L"Local\\CamCordDesktopRecorder");
    if (singleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"CamCordMainWindow", nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(singleInstance);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MainWindow window;
    if (!window.Create(instance, showCommand)) {
        MessageBoxW(nullptr, L"CamCord could not create its main window.", L"CamCord", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize();
        if (singleInstance) CloseHandle(singleInstance);
        return 1;
    }
    const int code = window.Run();
    if (SUCCEEDED(com)) CoUninitialize();
    if (singleInstance) CloseHandle(singleInstance);
    return code;
}
