#include "MainWindow.h"
#include "AppVersion.h"
#include "Resource.h"
#include "RecordingQuality.h"
#include "RegionSelector.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <cwctype>
#include <sstream>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
constexpr UINT_PTR TIMER_RECORDING = 1;
constexpr UINT WM_CHOOSE_FOLDER = WM_APP + 1;
constexpr UINT WM_FATAL_ERROR = WM_APP + 2;
constexpr UINT WM_INSTALL_UPDATE = WM_APP + 3;
constexpr UINT WM_PICK_REGION = WM_APP + 4;
constexpr UINT WM_SOURCE_DESTROYED = WM_APP + 5;
HWND sourceEventWindow = nullptr;
constexpr wchar_t APP_URL[] = L"https://app.camcord/index.html";

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> path(32768);
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), count)).parent_path().wstring();
}

std::wstring WebViewDataDirectory() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)) || !local) return L"";
    const auto path = (std::filesystem::path(local) / L"CamCord" / L"WebView2").wstring();
    CoTaskMemFree(local);
    return path;
}

size_t JsonValueStart(const std::wstring& json, const wchar_t* key) {
    const auto position = json.find(L"\"" + std::wstring(key) + L"\"");
    if (position == std::wstring::npos) return position;
    auto start = json.find(L':', position);
    if (start == std::wstring::npos) return start;
    while (++start < json.size() && iswspace(json[start])) {}
    return start;
}

std::wstring JsonString(const std::wstring& json, const wchar_t* key) {
    auto position = JsonValueStart(json, key);
    if (position >= json.size() || json[position] != L'"') return L"";
    const auto end = json.find(L'"', position + 1);
    return end == std::wstring::npos ? L"" : json.substr(position + 1, end - position - 1);
}

int JsonInteger(const std::wstring& json, const wchar_t* key, int fallback) {
    const auto position = JsonValueStart(json, key);
    if (position >= json.size()) return fallback;
    try {
        size_t consumed = 0;
        const int value = std::stoi(json.substr(position), &consumed);
        auto end = position + consumed;
        while (end < json.size() && iswspace(json[end])) ++end;
        // Never silently turn fractional/string-valued input into a valid preset.
        if (end >= json.size() || (json[end] != L',' && json[end] != L'}')) return fallback;
        return value;
    } catch (...) { return fallback; }
}

bool JsonBoolean(const std::wstring& json, const wchar_t* key, bool fallback) {
    const auto position = JsonValueStart(json, key);
    if (position >= json.size()) return fallback;
    if (json.compare(position, 4, L"true") == 0) return true;
    if (json.compare(position, 5, L"false") == 0) return false;
    return fallback;
}

std::wstring JsonEscape(const std::wstring& value) {
    std::wstring result;
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (const wchar_t c : value) {
        if (c == L'\\' || c == L'"') { result += L'\\'; result += c; }
        else if (c < 32) { result += L"\\u00"; result += digits[(c >> 4) & 15]; result += digits[c & 15]; }
        else result += c;
    }
    return result;
}

RecordingSettings SettingsFromJson(const std::wstring& json, RecordingSettings settings) {
    settings.height = JsonInteger(json, L"height", settings.height);
    if (settings.height != 480 && settings.height != 720 && settings.height != 1080) settings.height = 1080;
    settings.width = settings.height == 480 ? 854 : settings.height == 720 ? 1280 : 1920;
    settings.fps = JsonInteger(json, L"fps", settings.fps);
    if (settings.fps != 15 && settings.fps != 30 && settings.fps != 60 && settings.fps != 120) settings.fps = 60;
    if (settings.height != 1080 && settings.fps == 120) settings.fps = 60;
    settings.bitrateMbps = NormalizeBitrateMbps(JsonInteger(json, L"bitrateMbps", settings.bitrateMbps));
    settings.systemAudio = JsonBoolean(json, L"systemAudio", settings.systemAudio);
    settings.microphone = JsonBoolean(json, L"microphone", settings.microphone);
    settings.autoCheckUpdates = JsonBoolean(json, L"autoCheckUpdates", settings.autoCheckUpdates);
    return settings;
}
}

bool MainWindow::Create(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    settings_ = settingsManager_.Load();
    RefreshSources();
    settings_.captureTarget = PrimaryCaptureDisplay();
    startupEnabled_ = startup_.IsEnabled();
    nextUpdateCheck_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    recorder_.SetOutputFolder(settings_.outputFolder);
    outputFolder_ = settings_.outputFolder;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"CamCordMainWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_CAMCORD), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_CAMCORD), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassExW(&wc)) return false;
    const UINT dpi = GetDpiForSystem();
    RECT rect{ 0, 0, MulDiv(1040, dpi, 96), MulDiv(740, dpi, 96) };
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"CamCord", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    sourceEventWindow = hwnd_;
    sourceEvents_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr, SourceDestroyed, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    captureExcluded_ = SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void CALLBACK MainWindow::SourceDestroyed(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD eventTime) {
    if (sourceEventWindow && object == OBJID_WINDOW && child == CHILDID_SELF)
        PostMessageW(sourceEventWindow, WM_SOURCE_DESTROYED, reinterpret_cast<WPARAM>(window), static_cast<LPARAM>(eventTime));
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

void MainWindow::ReportFatal(std::wstring text) {
    fatalError_ = std::move(text);
    PostMessageW(hwnd_, WM_FATAL_ERROR, 0, 0);
}

void MainWindow::InitializeWebView() {
    const auto uiFolder = std::filesystem::path(ExecutableDirectory()) / L"ui";
    std::error_code ec;
    if (!std::filesystem::exists(uiFolder / L"index.html", ec)) {
        ReportFatal(L"The CamCord interface files are missing. Reinstall CamCord.");
        return;
    }
    const auto data = WebViewDataDirectory();
    const auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, data.empty() ? nullptr : data.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, uiFolder](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (!IsWindow(hwnd_)) return S_OK;
                if (FAILED(result) || !environment) {
                    ReportFatal(L"Microsoft Edge WebView2 Runtime could not start. Reinstall the runtime or rerun CamCord setup.");
                    return S_OK;
                }
                const auto controllerHr = environment->CreateCoreWebView2Controller(hwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, uiFolder](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!IsWindow(hwnd_)) return S_OK;
                            if (FAILED(controllerResult) || !controller) { ReportFatal(L"CamCord could not create its interface."); return S_OK; }
                            webViewController_ = controller;
                            controller->get_CoreWebView2(webView_.ReleaseAndGetAddressOf());
                            if (!webView_) { ReportFatal(L"CamCord could not initialize its interface."); return S_OK; }
                            ComPtr<ICoreWebView2Settings> preferences;
                            webView_->get_Settings(preferences.ReleaseAndGetAddressOf());
                            if (preferences) {
                                preferences->put_AreDefaultContextMenusEnabled(FALSE);
                                preferences->put_AreDevToolsEnabled(FALSE);
                                preferences->put_IsStatusBarEnabled(FALSE);
                                preferences->put_IsZoomControlEnabled(FALSE);
                            }
                            webView_->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    LPWSTR source = nullptr;
                                    const bool trusted = SUCCEEDED(args->get_Source(&source)) && source && std::wstring(source) == APP_URL;
                                    CoTaskMemFree(source);
                                    if (!trusted) return S_OK;
                                    LPWSTR message = nullptr;
                                    if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                                        if (wcslen(message) <= 4096) HandleWebMessage(message);
                                        CoTaskMemFree(message);
                                    }
                                    return S_OK;
                                }).Get(), &webMessageToken_);
                            EventRegistrationToken token{};
                            webView_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR uri = nullptr;
                                    if (SUCCEEDED(args->get_Uri(&uri))) {
                                        if (!uri || std::wstring(uri) != APP_URL) args->put_Cancel(TRUE);
                                        CoTaskMemFree(uri);
                                    }
                                    return S_OK;
                                }).Get(), &token);
                            webView_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    args->put_Handled(TRUE); return S_OK;
                                }).Get(), &token);
                            ComPtr<ICoreWebView2_3> view3;
                            if (FAILED(webView_.As(&view3)) || FAILED(view3->SetVirtualHostNameToFolderMapping(
                                L"app.camcord", uiFolder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
                                ReportFatal(L"Update Microsoft Edge WebView2 Runtime to display CamCord.");
                                return S_OK;
                            }
                            UpdateWebViewBounds();
                            webViewController_->put_IsVisible(TRUE);
                            if (FAILED(webView_->Navigate(APP_URL))) ReportFatal(L"CamCord could not load its interface.");
                            return S_OK;
                        }).Get());
                if (FAILED(controllerHr)) ReportFatal(L"CamCord could not create its interface.");
                return S_OK;
            }).Get());
    if (FAILED(hr)) ReportFatal(L"Microsoft Edge WebView2 Runtime is required. Rerun CamCord setup.");
}

void MainWindow::UpdateWebViewBounds() {
    if (webViewController_) { RECT rect{}; GetClientRect(hwnd_, &rect); webViewController_->put_Bounds(rect); }
}

void MainWindow::SetNotice(std::wstring severity, std::wstring text) {
    noticeSeverity_ = std::move(severity);
    noticeText_ = std::move(text);
}

void MainWindow::SaveSettings() {
    const auto result = settingsManager_.Save(settings_);
    if (!result.ok) SetNotice(L"error", result.message);
}

void MainWindow::UpdateSnapshot() {
    // The recorder is owned exclusively by the worker while an operation is pending.
    if (pending_ != Action::None) return;
    snapshotState_ = recorder_.State();
    elapsedSeconds_ = recorder_.ElapsedSeconds();
    encoder_ = recorder_.EncoderLabel();
    outputFolder_ = recorder_.CaptureFolder();
    lastOutput_ = recorder_.LastOutput();
}

void MainWindow::SendState(bool force) {
    if (!webReady_ || !webView_) return;
    std::wstring state = L"idle";
    if (pending_ == Action::Stop) state = L"saving";
    else if (pending_ == Action::Pause) state = L"pausing";
    else if (pending_ == Action::Resume) state = L"resuming";
    else if (pending_ != Action::None) state = L"starting";
    else if (snapshotState_ == RecorderState::Recording) state = L"recording";
    else if (snapshotState_ == RecorderState::Paused) state = L"paused";
    const auto lastName = lastOutput_.empty() ? L"" : std::filesystem::path(lastOutput_).filename().wstring();
    auto target = settings_.captureTarget;
    const bool sourceReady = !selectionInvalid_ && ResolveCaptureTarget(target).ok;
    const auto kindName = [](CaptureKind kind) { return kind == CaptureKind::Window ? L"window" : kind == CaptureKind::Region ? L"region" : L"display"; };
    std::wostringstream json;
    json << L"{\"type\":\"state\",\"state\":\"" << state << L"\",\"elapsedSeconds\":" << elapsedSeconds_
        << L",\"settings\":{\"height\":" << settings_.height << L",\"fps\":" << settings_.fps
        << L",\"bitrateMbps\":" << settings_.bitrateMbps
        << L",\"systemAudio\":" << (settings_.systemAudio ? L"true" : L"false")
        << L",\"microphone\":" << (settings_.microphone ? L"true" : L"false")
        << L",\"autoCheckUpdates\":" << (settings_.autoCheckUpdates ? L"true" : L"false")
        << L"},\"encoder\":\"" << JsonEscape(encoder_) << L"\",\"outputFolder\":\""
        << JsonEscape(outputFolder_.empty() ? L"Videos / CamCord Captures" : outputFolder_)
        << L"\",\"lastOutput\":\"" << JsonEscape(lastName) << L"\",\"captureExcluded\":"
        << (captureExcluded_ ? L"true" : L"false");
    json << L",\"appVersion\":\"" << CAMCORD_VERSION_W << L"\",\"startupEnabled\":" << (startupEnabled_ ? L"true" : L"false")
        << L",\"update\":{\"status\":\"" << JsonEscape(updateStatus_) << L"\",\"message\":\"" << JsonEscape(updateMessage_)
        << L"\",\"version\":\"" << JsonEscape(std::wstring(updateInfo_.version.begin(), updateInfo_.version.end()))
        << L"\",\"progress\":" << updater_.Progress() << L"}";
    json << L",\"selectingSource\":" << (selectingSource_ ? L"true" : L"false")
        << L",\"captureSource\":{\"kind\":\"" << kindName(target.kind) << L"\",\"id\":\""
        << (target.handle ? CaptureSourceId(target) : L"") << L"\",\"label\":\"" << JsonEscape(target.label)
        << L"\",\"ready\":" << (sourceReady ? L"true" : L"false")
        << L",\"width\":" << (target.kind == CaptureKind::Region ? target.regionWidth : target.sourceWidth)
        << L",\"height\":" << (target.kind == CaptureKind::Region ? target.regionHeight : target.sourceHeight) << L"},\"sources\":[";
    bool firstSource = true;
    for (const auto& entry : captureSources_) {
        if (!firstSource) json << L",";
        firstSource = false;
        json << L"{\"id\":\"" << entry.id << L"\",\"kind\":\"" << kindName(entry.target.kind)
            << L"\",\"label\":\"" << JsonEscape(entry.target.label) << L"\",\"primary\":" << (entry.primary ? L"true" : L"false") << L"}";
    }
    json << L"]";
    if (!noticeText_.empty()) json << L",\"notice\":{\"severity\":\"" << JsonEscape(noticeSeverity_)
        << L"\",\"text\":\"" << JsonEscape(noticeText_) << L"\"}";
    json << L"}";
    const auto payload = json.str();
    if (force || payload != lastStateJson_) {
        if (SUCCEEDED(webView_->PostWebMessageAsJson(payload.c_str()))) {
            lastStateJson_ = payload; noticeText_.clear(); noticeSeverity_.clear();
        }
    }
}

void MainWindow::BeginAction(Action action, const std::wstring& folder) {
    if (pending_ != Action::None) return;
    UpdateSnapshot();
    pending_ = action;
    SendState();
    ShutdownBlockReasonCreate(hwnd_, L"CamCord is recording or saving a video. Finish saving before shutting down.");
    try {
        task_ = std::async(std::launch::async, [this, action, copy = settings_, folder]() mutable -> ActionResult {
            OperationResult result;
            try {
                switch (action) {
                case Action::Initialize: result = recorder_.Initialize(); break;
                case Action::Start: result = recorder_.Start(copy); break;
                case Action::Pause: result = recorder_.Pause(); break;
                case Action::Resume: result = recorder_.Resume(); break;
                case Action::Stop: result = recorder_.Stop(); break;
                case Action::SetFolder: {
                    OutputManager candidate;
                    candidate.SetCaptureFolder(folder);
                    result = candidate.EnsureCaptureFolder();
                    if (result.ok) { recorder_.SetOutputFolder(folder); copy.outputFolder = folder; }
                    break;
                }
                default: result = OperationResult::Failure(L"Unknown recorder action."); break;
                }
            } catch (...) {
                result = OperationResult::Failure(L"An unexpected recording error occurred. Session files were kept in the recording folder's .camcord-sessions directory.");
            }
            return { std::move(result), std::move(copy) };
        });
    } catch (...) {
        pending_ = Action::None;
        if (!fatalCloseRequested_) closeRequested_ = false;
        if (snapshotState_ == RecorderState::Idle) ShutdownBlockReasonDestroy(hwnd_);
        SetNotice(L"error", L"CamCord could not start its background task. Try again.");
        SendState();
    }
}

void MainWindow::PollAction() {
    if (pending_ == Action::None || !task_.valid() || task_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    const Action completed = pending_;
    auto result = task_.get();
    pending_ = Action::None;
    settings_ = std::move(result.settings);
    UpdateSnapshot();
    if (!result.result.ok) {
        if (!fatalCloseRequested_) closeRequested_ = false;
        SetNotice(L"error", result.result.message);
    }
    else if (completed == Action::Stop) {
        SetNotice(unexpectedStop_ ? L"warning" : L"success", unexpectedStop_
            ? (sourceLost_ ? L"The selected source closed, was minimized or disconnected. The completed portion was saved."
                : L"Recording stopped unexpectedly. The completed portion was recovered.")
            : L"Recording saved successfully.");
    } else if (completed == Action::SetFolder) SetNotice(L"success", L"New recordings will be saved in the selected folder.");
    if (completed == Action::Start || completed == Action::SetFolder) SaveSettings();
    unexpectedStop_ = false;
    sourceLost_ = false;
    if (snapshotState_ == RecorderState::Idle) ShutdownBlockReasonDestroy(hwnd_);
    SendState();
    if (closeRequested_) {
        if (snapshotState_ == RecorderState::Idle) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        else BeginAction(Action::Stop);
    }
}

void MainWindow::BeginUpdate(UpdateAction action, bool manual) {
    if (updateAction_ != UpdateAction::None || closeRequested_) return;
    if (action == UpdateAction::Verify && !UpdateManager::CanInstall(snapshotState_, pending_ != Action::None,
        folderDialogOpen_, updateStatus_ == L"ready")) {
        SetNotice(L"info", L"Stop and save your recording before installing an update."); SendState(); return;
    }
    updater_.Reset();
    updateAction_ = action;
    manualUpdate_ = manual;
    if (action == UpdateAction::Check) {
        updateStatus_ = L"checking"; updateMessage_ = L"Checking for updates...";
        downloadWhenAvailable_ = manual || settings_.autoCheckUpdates;
        nextUpdateCheck_ = std::chrono::steady_clock::now() + std::chrono::hours(6);
    } else if (action == UpdateAction::Download) {
        updateStatus_ = L"downloading"; updateMessage_ = L"Downloading update... You can keep recording.";
        downloadWhenAvailable_ = false;
    } else {
        updateStatus_ = L"installing"; updateMessage_ = L"Verifying update before restarting...";
    }
    SendState();
    try {
        updateTask_ = std::async(std::launch::async, [this, action, info = updateInfo_, installer = updateInstaller_]() -> UpdateResult {
            try {
                if (action == UpdateAction::Check) return updater_.Check();
                if (action == UpdateAction::Download) return updater_.Download(info);
                return { updater_.Verify(info, installer), info, installer };
            } catch (...) { return { OperationResult::Failure(L"The update could not be prepared. Try again later."), {}, L"" }; }
        });
    } catch (...) {
        updateAction_ = UpdateAction::None;
        updateStatus_ = L"error"; updateMessage_ = L"Could not start the update check. Try again.";
        SendState();
    }
}

void MainWindow::PollUpdate() {
    if (updateAction_ != UpdateAction::None && updateTask_.valid() &&
        updateTask_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const auto completed = updateAction_;
        UpdateResult result;
        try { result = updateTask_.get(); }
        catch (...) { result.result = OperationResult::Failure(L"The update could not be prepared. Try again later."); }
        updateAction_ = UpdateAction::None;
        if (closeRequested_) { PostMessageW(hwnd_, WM_CLOSE, 0, 0); return; }
        if (updater_.Cancelled() && !manualUpdate_) {
            updateStatus_ = L"idle"; updateMessage_ = L"Automatic updates are off."; downloadWhenAvailable_ = false;
        } else if (!result.result.ok) {
            updateStatus_ = L"error"; updateMessage_ = result.result.message; downloadWhenAvailable_ = false;
        } else if (completed == UpdateAction::Check) {
            updateInfo_ = result.info; updateInstaller_.clear();
            updateStatus_ = updateInfo_.available ? L"available" : L"idle";
            updateMessage_ = result.result.message;
        } else if (completed == UpdateAction::Download) {
            updateInstaller_ = std::move(result.installer);
            updateStatus_ = L"ready"; updateMessage_ = L"Update ready. Install when you've finished recording.";
        } else {
            PostMessageW(hwnd_, WM_INSTALL_UPDATE, 0, 0);
        }
        SendState();
    }
    if (closeRequested_ || updateAction_ != UpdateAction::None || !webReady_) return;
    if (updateStatus_ == L"available" && downloadWhenAvailable_ && pending_ == Action::None &&
        snapshotState_ == RecorderState::Idle && !folderDialogOpen_) {
        BeginUpdate(UpdateAction::Download, manualUpdate_);
    } else if (updateStatus_ != L"installing" && settings_.autoCheckUpdates &&
        std::chrono::steady_clock::now() >= nextUpdateCheck_) BeginUpdate(UpdateAction::Check);
}

void MainWindow::InstallUpdate() {
    if (closeRequested_ || !UpdateManager::CanInstall(snapshotState_, pending_ != Action::None,
        folderDialogOpen_, updateStatus_ == L"installing")) return;
    const auto folder = ExecutableDirectory();
    const auto parameters = L"/SP- /SILENT /NORESTART /CAMCORDUPDATE=1 /DIR=" + QuoteArg(folder);
    SHELLEXECUTEINFOW launch{ sizeof(launch) };
    launch.hwnd = hwnd_; launch.lpVerb = L"open"; launch.lpFile = updateInstaller_.c_str();
    launch.lpParameters = parameters.c_str(); launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch)) {
        updateStatus_ = L"ready"; updateMessage_ = L"The update installer could not open. Try Install & restart again.";
        SetNotice(L"error", updateMessage_); SendState(); return;
    }
    closeRequested_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void MainWindow::ChooseOutputFolder() {
    if (pending_ != Action::None || snapshotState_ != RecorderState::Idle || folderDialogOpen_) return;
    folderDialogOpen_ = true;
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr)) {
        FILEOPENDIALOGOPTIONS options{};
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"Choose where CamCord saves recordings");
        ComPtr<IShellItem> current;
        if (!outputFolder_.empty() && SUCCEEDED(SHCreateItemFromParsingName(outputFolder_.c_str(), nullptr,
            IID_PPV_ARGS(current.ReleaseAndGetAddressOf())))) dialog->SetFolder(current.Get());
        hr = dialog->Show(hwnd_);
        if (SUCCEEDED(hr)) {
            ComPtr<IShellItem> selected;
            PWSTR path = nullptr;
            hr = dialog->GetResult(selected.ReleaseAndGetAddressOf());
            if (SUCCEEDED(hr) && selected) hr = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(hr) && path) BeginAction(Action::SetFolder, path);
            CoTaskMemFree(path);
        }
    }
    folderDialogOpen_ = false;
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        SetNotice(L"error", L"Windows could not select that folder."); SendState();
    }
}

void MainWindow::RefreshSources() {
    captureSources_ = EnumerateCaptureSources();
}

void MainWindow::ChooseSourceRegion() {
    if (pending_ != Action::None || snapshotState_ != RecorderState::Idle || folderDialogOpen_ || closeRequested_ ||
        updateStatus_ == L"installing" || settings_.captureTarget.kind != CaptureKind::Region) { SendState(true); return; }
    auto display = settings_.captureTarget; display.kind = CaptureKind::Display;
    const auto available = ResolveCaptureTarget(display);
    if (!available.ok) { SetNotice(L"error", available.message); SendState(true); return; }
    folderDialogOpen_ = true; selectingSource_ = true; SendState(true);
    CaptureTarget selected;
    if (ChooseCaptureRegion(hwnd_, display, selected)) { settings_.captureTarget = std::move(selected); selectionInvalid_ = false; }
    folderDialogOpen_ = false; selectingSource_ = false;
    SendState(true);
}

void MainWindow::HandleWebMessage(const std::wstring& message) {
    const auto type = JsonString(message, L"type");
    if (type == L"ready") { webReady_ = true; RefreshSources(); SendState(true); return; }
    if (closeRequested_ || updateStatus_ == L"installing") { SendState(true); return; }
    if (type == L"checkUpdates") { BeginUpdate(UpdateAction::Check, true); return; }
    if (type == L"downloadUpdate" && updateStatus_ == L"available") { BeginUpdate(UpdateAction::Download, true); return; }
    if (type == L"installUpdate") { BeginUpdate(UpdateAction::Verify, true); return; }
    if (type == L"openOutput") {
        if (!outputFolder_.empty()) ShellExecuteW(hwnd_, L"open", outputFolder_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (pending_ != Action::None || folderDialogOpen_) { SendState(true); return; }
    if (snapshotState_ == RecorderState::Idle && type == L"refreshSources") { RefreshSources(); SendState(true); }
    else if (snapshotState_ == RecorderState::Idle && type == L"sourceMode") {
        const auto kind = JsonString(message, L"kind");
        RefreshSources();
        auto display = PrimaryCaptureDisplay();
        if (settings_.captureTarget.kind != CaptureKind::Window) {
            for (const auto& entry : captureSources_) if (entry.target.kind == CaptureKind::Display &&
                entry.target.handle == settings_.captureTarget.handle) { display = entry.target; break; }
        }
        if (kind == L"display") settings_.captureTarget = display;
        else if (kind == L"window") { settings_.captureTarget = {}; settings_.captureTarget.kind = CaptureKind::Window; settings_.captureTarget.label = L"Choose a window"; }
        else if (kind == L"region") {
            settings_.captureTarget = display; settings_.captureTarget.kind = CaptureKind::Region;
            settings_.captureTarget.label = L"Select an area";
        } else { SendState(true); return; }
        selectionInvalid_ = false;
        sourceSelectedAt_ = GetTickCount();
        RefreshSources(); SendState(true);
    } else if (snapshotState_ == RecorderState::Idle && type == L"selectSource") {
        const auto id = JsonString(message, L"id");
        const bool windowMode = settings_.captureTarget.kind == CaptureKind::Window;
        for (const auto& entry : captureSources_) if (entry.id == id && (entry.target.kind == CaptureKind::Window) == windowMode) {
            if (windowMode || settings_.captureTarget.kind == CaptureKind::Display) settings_.captureTarget = entry.target;
            else { settings_.captureTarget = entry.target; settings_.captureTarget.kind = CaptureKind::Region; settings_.captureTarget.label = L"Select an area"; }
            selectionInvalid_ = false; sourceSelectedAt_ = GetTickCount(); SendState(true); return;
        }
        SetNotice(L"warning", L"That source is no longer in the list. Refresh and choose it again."); SendState(true);
    } else if (snapshotState_ == RecorderState::Idle && type == L"pickRegion" && settings_.captureTarget.kind == CaptureKind::Region) {
        PostMessageW(hwnd_, WM_PICK_REGION, 0, 0);
    } else if (type == L"settings" && snapshotState_ == RecorderState::Idle) {
        const bool wasAutomatic = settings_.autoCheckUpdates;
        settings_ = SettingsFromJson(message, settings_);
        if (wasAutomatic != settings_.autoCheckUpdates) {
            nextUpdateCheck_ = std::chrono::steady_clock::now();
            if (!settings_.autoCheckUpdates && !manualUpdate_) { downloadWhenAvailable_ = false; updater_.Cancel(); }
        }
        SaveSettings(); SendState();
    } else if (type == L"startup" && snapshotState_ == RecorderState::Idle) {
        const auto result = startup_.SetEnabled(JsonBoolean(message, L"enabled", startupEnabled_));
        startupEnabled_ = startup_.IsEnabled();
        if (!result.ok) SetNotice(L"error", result.message);
        SendState(true);
    } else if (type == L"start" && snapshotState_ == RecorderState::Idle) {
        if (selectionInvalid_) { SetNotice(L"warning", L"The source changed or closed. Select it again before recording."); SendState(true); return; }
        settings_ = SettingsFromJson(message, settings_); SaveSettings(); BeginAction(Action::Start);
    } else if (type == L"pause") {
        if (snapshotState_ == RecorderState::Recording) BeginAction(Action::Pause);
        else if (snapshotState_ == RecorderState::Paused) BeginAction(Action::Resume);
    } else if (type == L"stop" && snapshotState_ != RecorderState::Idle) {
        BeginAction(Action::Stop);
    } else if (type == L"chooseOutput" && snapshotState_ == RecorderState::Idle) {
        // Native modal dialogs must start after WebMessageReceived has returned.
        PostMessageW(hwnd_, WM_CHOOSE_FOLDER, 0, 0);
    } else if (type == L"openLast" && !lastOutput_.empty()) {
        const auto arguments = L"/select,\"" + lastOutput_ + L"\"";
        ShellExecuteW(hwnd_, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
    } else SendState(true);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        SetTimer(hwnd_, TIMER_RECORDING, 200, nullptr);
        BeginAction(Action::Initialize);
        InitializeWebView();
        return 0;
    case WM_SIZE: UpdateWebViewBounds(); return 0;
    case WM_DPICHANGED: {
        const auto rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateWebViewBounds(); return 0;
    }
    case WM_GETMINMAXINFO: {
        const auto info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = GetDpiForWindow(hwnd_);
        info->ptMinTrackSize.x = MulDiv(620, dpi ? dpi : 96, 96);
        info->ptMinTrackSize.y = MulDiv(600, dpi ? dpi : 96, 96);
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_RECORDING) {
            PollAction();
            if (!IsWindow(hwnd_)) return 0;
            PollUpdate();
            if (pending_ == Action::None) {
                UpdateSnapshot();
                const auto& target = settings_.captureTarget;
                const bool missing = selectionInvalid_ || !CaptureSourceExists(target) || (target.kind == CaptureKind::Window &&
                    (IsIconic(reinterpret_cast<HWND>(target.handle)) || !IsWindowVisible(reinterpret_cast<HWND>(target.handle))));
                if ((snapshotState_ == RecorderState::Recording || snapshotState_ == RecorderState::Paused) && missing) {
                    selectionInvalid_ = true; sourceLost_ = true; unexpectedStop_ = true; BeginAction(Action::Stop);
                } else if (snapshotState_ == RecorderState::Recording && !recorder_.CaptureAlive()) {
                    unexpectedStop_ = true; BeginAction(Action::Stop);
                }
            }
            SendState();
        }
        return 0;
    case WM_CHOOSE_FOLDER: ChooseOutputFolder(); return 0;
    case WM_PICK_REGION: ChooseSourceRegion(); return 0;
    case WM_SOURCE_DESTROYED:
        if (settings_.captureTarget.kind == CaptureKind::Window && settings_.captureTarget.handle == static_cast<unsigned long long>(wParam) &&
            static_cast<LONG>(static_cast<DWORD>(lParam) - sourceSelectedAt_) >= 0) {
            selectionInvalid_ = true; SendState(true);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        if (!CaptureSourceExists(settings_.captureTarget)) selectionInvalid_ = true;
        RefreshSources(); SendState(true); return 0;
    case WM_INSTALL_UPDATE: InstallUpdate(); return 0;
    case WM_FATAL_ERROR:
        MessageBoxW(hwnd_, fatalError_.c_str(), L"CamCord", MB_OK | MB_ICONERROR);
        fatalCloseRequested_ = true;
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return 0;
    case WM_QUERYENDSESSION:
        return pending_ == Action::None && snapshotState_ == RecorderState::Idle;
    case WM_CLOSE:
        updater_.Cancel();
        if (pending_ != Action::None || snapshotState_ != RecorderState::Idle) {
            closeRequested_ = true;
            SetNotice(L"info", L"CamCord will close after your recording finishes saving.");
            if (pending_ == Action::None) BeginAction(Action::Stop);
            SendState();
            return 0;
        }
        if (updateTask_.valid()) {
            closeRequested_ = true;
            SetNotice(L"info", L"Closing CamCord..."); SendState(); return 0;
        }
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        sourceEventWindow = nullptr;
        if (sourceEvents_) { UnhookWinEvent(sourceEvents_); sourceEvents_ = nullptr; }
        KillTimer(hwnd_, TIMER_RECORDING);
        ShutdownBlockReasonDestroy(hwnd_);
        if (task_.valid()) task_.wait();
        updater_.Cancel();
        if (updateTask_.valid()) updateTask_.wait();
        if (webView_ && webMessageToken_.value) webView_->remove_WebMessageReceived(webMessageToken_);
        if (webViewController_) webViewController_->Close();
        webView_.Reset(); webViewController_.Reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
