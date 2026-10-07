#include "OutputManager.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <iomanip>
#include <sstream>

OperationResult OutputManager::EnsureCaptureFolder() {
    if (captureFolder_.empty()) {
        PWSTR videos = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &videos);
        if (FAILED(hr) || !videos) return OperationResult::Failure(L"Windows Videos folder could not be found.");
        captureFolder_ = (std::filesystem::path(videos) / L"CamCord Captures").wstring();
        CoTaskMemFree(videos);
    }
    if (!std::filesystem::path(captureFolder_).is_absolute())
        return OperationResult::Failure(L"Choose an absolute recording folder path.");
    std::error_code ec;
    std::filesystem::create_directories(captureFolder_, ec);
    if (ec || !std::filesystem::is_directory(captureFolder_, ec) || ec)
        return OperationResult::Failure(L"The recording folder is unavailable. Connect the drive or choose another folder.");
    const auto probe = (std::filesystem::path(captureFolder_) / (L".camcord-write-test-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()))).wstring();
    HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return OperationResult::Failure(L"CamCord cannot write to this folder. Choose a writable folder or drive.");
    CloseHandle(file);
    return OperationResult::Success(captureFolder_);
}

OperationResult OutputManager::EnsureFreeSpace(unsigned long long minimumBytes) const {
    ULARGE_INTEGER available{}, total{}, freeTotal{};
    if (captureFolder_.empty() || !GetDiskFreeSpaceExW(captureFolder_.c_str(), &available, &total, &freeTotal))
        return OperationResult::Failure(L"Available disk space could not be checked.");
    if (available.QuadPart < minimumBytes)
        return OperationResult::Failure(L"There is not enough free space to start recording. Free at least 512 MB and try again.");
    return OperationResult::Success();
}

static std::wstring Timestamp() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    std::wostringstream s;
    s << std::setfill(L'0') << t.wYear << L'-' << std::setw(2) << t.wMonth << L'-' << std::setw(2) << t.wDay
      << L'_' << std::setw(2) << t.wHour << L'-' << std::setw(2) << t.wMinute << L'-' << std::setw(2) << t.wSecond;
    return s.str();
}

std::wstring OutputManager::NewOutputPath() const {
    auto base = std::filesystem::path(captureFolder_) / (L"CamCord_" + Timestamp() + L".mp4");
    if (!std::filesystem::exists(base)) return base.wstring();
    for (int i = 2; i < 10000; ++i) {
        auto candidate = base.parent_path() / (base.stem().wstring() + L"_" + std::to_wstring(i) + L".mp4");
        if (!std::filesystem::exists(candidate)) return candidate.wstring();
    }
    return (base.parent_path() / (L"CamCord_" + Timestamp() + L"_unique.mp4")).wstring();
}

std::wstring OutputManager::NewSessionFolder() const {
    auto path = std::filesystem::path(captureFolder_) / L".camcord-sessions" /
        (Timestamp() + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return ec ? L"" : path.wstring();
}
