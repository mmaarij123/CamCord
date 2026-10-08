#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include "RecordingTypes.h"
#include "AppVersion.h"

struct UpdateInfo {
    std::string version, url, sha256;
    unsigned long long size = 0;
    bool available = false;
};

struct UpdateResult {
    OperationResult result;
    UpdateInfo info;
    std::wstring installer;
};

class UpdateManager {
public:
    void Cancel() { cancelled_ = true; }
    void Reset() { cancelled_ = false; progress_ = 0; }
    int Progress() const { return progress_.load(); }
    bool Cancelled() const { return cancelled_.load(); }
    UpdateResult Check(const std::string& currentVersion = CAMCORD_VERSION);
    UpdateResult Download(const UpdateInfo& info, const std::wstring& cacheDirectory = L"");
    OperationResult Verify(const UpdateInfo& info, const std::wstring& installer) const;
    static OperationResult ParseRelease(const std::string& document, const std::string& currentVersion, UpdateInfo& info);
    static bool NewerVersion(const std::string& candidate, const std::string& current);
    static std::string FileSha256(const std::wstring& path);
    static bool CanInstall(RecorderState state, bool pending, bool folderDialog, bool ready) {
        return state == RecorderState::Idle && !pending && !folderDialog && ready;
    }
private:
    std::atomic<bool> cancelled_{ false };
    std::atomic<int> progress_{ 0 };
};
