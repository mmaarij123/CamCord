#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <future>
#include <string>
#include "RecordingManager.h"
#include "SettingsManager.h"
#include "StartupManager.h"
#include "UpdateManager.h"
#include "CaptureSources.h"

class MainWindow {
public:
    bool Create(HINSTANCE instance, int showCommand);
    int Run();
private:
    enum class Action { None, Initialize, Start, Pause, Resume, Stop, SetFolder };
    struct ActionResult { OperationResult result; RecordingSettings settings; };
    enum class UpdateAction { None, Check, Download, Verify };
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK SourceDestroyed(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void InitializeWebView();
    void UpdateWebViewBounds();
    void HandleWebMessage(const std::wstring& message);
    void BeginAction(Action action, const std::wstring& folder = L"");
    void PollAction();
    void UpdateSnapshot();
    void SendState(bool force = false);
    void ChooseOutputFolder();
    void RefreshSources();
    void ChooseSourceRegion();
    void SetNotice(std::wstring severity, std::wstring text);
    void ReportFatal(std::wstring text);
    void SaveSettings();
    void BeginUpdate(UpdateAction action, bool manual = false);
    void PollUpdate();
    void InstallUpdate();
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> webViewController_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView_;
    EventRegistrationToken webMessageToken_{};
    SettingsManager settingsManager_;
    RecordingManager recorder_;
    RecordingSettings settings_;
    std::future<ActionResult> task_;
    Action pending_ = Action::None;
    RecorderState snapshotState_ = RecorderState::Idle;
    unsigned long long elapsedSeconds_ = 0;
    std::wstring encoder_, outputFolder_, lastOutput_, lastStateJson_;
    std::wstring noticeSeverity_, noticeText_, fatalError_;
    bool webReady_ = false;
    bool closeRequested_ = false;
    bool unexpectedStop_ = false;
    bool folderDialogOpen_ = false;
    bool captureExcluded_ = false;
    bool fatalCloseRequested_ = false;
    bool selectingSource_ = false, sourceLost_ = false, selectionInvalid_ = false;
    HWINEVENTHOOK sourceEvents_ = nullptr;
    DWORD sourceSelectedAt_ = 0;
    std::vector<CaptureSourceEntry> captureSources_;
    StartupManager startup_;
    UpdateManager updater_;
    std::future<UpdateResult> updateTask_;
    UpdateAction updateAction_ = UpdateAction::None;
    UpdateInfo updateInfo_;
    std::wstring updateStatus_ = L"idle", updateMessage_ = L"Check for updates.", updateInstaller_;
    std::chrono::steady_clock::time_point nextUpdateCheck_;
    bool startupEnabled_ = false, manualUpdate_ = false, downloadWhenAvailable_ = false;
};
