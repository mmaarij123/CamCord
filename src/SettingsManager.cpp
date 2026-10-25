#include "SettingsManager.h"
#include "RecordingQuality.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <sstream>
#include <vector>

std::wstring SettingsManager::SettingsPath() const {
    if (!pathOverride_.empty()) return pathOverride_;
    PWSTR raw = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw))) {
        base = raw;
        CoTaskMemFree(raw);
    } else {
        wchar_t fallback[MAX_PATH]{};
        GetTempPathW(MAX_PATH, fallback);
        base = fallback;
    }
    auto dir = std::filesystem::path(base) / L"CamCord";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / L"settings.ini").wstring();
}

RecordingSettings SettingsManager::Load() const {
    RecordingSettings s;
    const auto path = SettingsPath();
    s.height = GetPrivateProfileIntW(L"Recording", L"Height", 1080, path.c_str());
    if (s.height != 480 && s.height != 720 && s.height != 1080) s.height = 1080;
    s.width = s.height == 480 ? 854 : (s.height == 720 ? 1280 : 1920);
    s.fps = GetPrivateProfileIntW(L"Recording", L"Fps", 60, path.c_str());
    if (s.fps != 15 && s.fps != 30 && s.fps != 60 && s.fps != 120) s.fps = 60;
    if (s.fps == 120 && s.height != 1080) s.fps = 60;
    wchar_t bitrate[64]{};
    GetPrivateProfileStringW(L"Recording", L"BitrateMbps", L"0", bitrate, 64, path.c_str());
    s.bitrateMbps = BitrateFromPreference(bitrate);
    s.systemAudio = GetPrivateProfileIntW(L"Recording", L"SystemAudio", 1, path.c_str()) != 0;
    s.microphone = GetPrivateProfileIntW(L"Recording", L"Microphone", 0, path.c_str()) != 0;
    s.autoCheckUpdates = GetPrivateProfileIntW(L"Application", L"AutoCheckUpdates", 1, path.c_str()) != 0;
    std::vector<wchar_t> folder(32768);
    GetPrivateProfileStringW(L"Recording", L"OutputFolder", L"", folder.data(), static_cast<DWORD>(folder.size()), path.c_str());
    s.outputFolder = folder.data();
    return s;
}

OperationResult SettingsManager::Save(const RecordingSettings& s) const {
    const auto path = SettingsPath();
    if (s.outputFolder.find_first_of(L"\r\n") != std::wstring::npos)
        return OperationResult::Failure(L"The recording folder contains invalid characters.");
    std::wostringstream content;
    content << L"\xFEFF[Recording]\r\nHeight=" << s.height << L"\r\nFps=" << s.fps
        << L"\r\nBitrateMbps=" << NormalizeBitrateMbps(s.bitrateMbps)
        << L"\r\nSystemAudio=" << (s.systemAudio ? 1 : 0) << L"\r\nMicrophone=" << (s.microphone ? 1 : 0)
        << L"\r\nOutputFolder=" << s.outputFolder << L"\r\n[Application]\r\nAutoCheckUpdates=" << (s.autoCheckUpdates ? 1 : 0) << L"\r\n";
    const auto text = content.str();
    const auto temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return OperationResult::Failure(L"Recording settings could not be saved.");
    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    const bool ok = WriteFile(file, text.data(), bytes, &written, nullptr) && written == bytes && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return OperationResult::Failure(L"Recording settings could not be saved.");
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return OperationResult::Success();
}
