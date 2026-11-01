#include "SettingsManager.h"
#include "RecordingQuality.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <sstream>
#include <vector>
#include <atomic>

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
    s.height = NormalizeRecordingHeight(s.height);
    s.width = RecordingWidth(s.height);
    s.fps = GetPrivateProfileIntW(L"Recording", L"Fps", 60, path.c_str());
    s.fps = NormalizeRecordingFps(s.height, s.fps);
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
    static std::atomic<unsigned long long> serial{0};
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++serial) + L".tmp";
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (file == INVALID_HANDLE_VALUE) return OperationResult::Failure(L"Recording settings could not be saved. Windows error: " + std::to_wstring(GetLastError()));
    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    const bool ok = WriteFile(file, text.data(), bytes, &written, nullptr) && written == bytes && FlushFileBuffers(file);
    const DWORD writeError = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    bool published = false;
    DWORD moveError = ERROR_SUCCESS;
    if (ok) for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { published = true; break; }
        moveError = GetLastError();
        // INI readers and scanners can briefly hold the previous file open.
        // Preserve atomic replacement; never delete the old preferences first.
        if (moveError != ERROR_SHARING_VIOLATION && moveError != ERROR_LOCK_VIOLATION && moveError != ERROR_ACCESS_DENIED) break;
        if (attempt < 4) Sleep(10u << attempt);
    }
    if (!ok || !published) {
        DeleteFileW(temporary.c_str());
        return OperationResult::Failure(L"Recording settings could not be saved. Windows error: " + std::to_wstring(ok ? moveError : writeError));
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return OperationResult::Success();
}
