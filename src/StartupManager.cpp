#include "StartupManager.h"
#include <filesystem>
#include <vector>

StartupManager::StartupManager(std::wstring key, std::wstring executable)
    : key_(std::move(key)), executable_(std::move(executable)) {
    if (executable_.empty()) {
        std::vector<wchar_t> path(32768);
        const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (size && size < path.size()) executable_.assign(path.data(), size);
    }
}

std::wstring StartupManager::Command() const {
    if (executable_.empty() || executable_.find_first_of(L"\"\r\n") != std::wstring::npos ||
        !std::filesystem::path(executable_).is_absolute()) return L"";
    return L"\"" + executable_ + L"\" --startup";
}

bool StartupManager::IsEnabled() const {
    const auto expected = Command();
    if (expected.empty()) return false;
    DWORD type = 0, bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key_.c_str(), L"CamCord", RRF_RT_REG_SZ,
        &type, nullptr, &bytes) != ERROR_SUCCESS || bytes > 65536 || bytes % sizeof(wchar_t)) return false;
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1);
    if (RegGetValueW(HKEY_CURRENT_USER, key_.c_str(), L"CamCord", RRF_RT_REG_SZ,
        &type, value.data(), &bytes) != ERROR_SUCCESS) return false;
    return _wcsicmp(value.data(), expected.c_str()) == 0;
}

OperationResult StartupManager::SetEnabled(bool enabled) const {
    HKEY key = nullptr;
    if (!enabled) {
        const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, key_.c_str(), 0, KEY_SET_VALUE, &key);
        if (opened == ERROR_FILE_NOT_FOUND) return OperationResult::Success();
        if (opened != ERROR_SUCCESS) return OperationResult::Failure(L"Windows startup settings could not be changed.");
        const auto removed = RegDeleteValueW(key, L"CamCord");
        RegCloseKey(key);
        return removed == ERROR_SUCCESS || removed == ERROR_FILE_NOT_FOUND ? OperationResult::Success()
            : OperationResult::Failure(L"CamCord could not be removed from Windows startup.");
    }
    const auto command = Command();
    if (command.empty() || command.size() > 260)
        return OperationResult::Failure(L"Install CamCord in a shorter folder before enabling Windows startup.");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(executable_, ec) || ec)
        return OperationResult::Failure(L"The CamCord application could not be found for Windows startup.");
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key_.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return OperationResult::Failure(L"Windows startup settings could not be changed.");
    const auto written = RegSetValueExW(key, L"CamCord", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return written == ERROR_SUCCESS ? OperationResult::Success()
        : OperationResult::Failure(L"CamCord could not be added to Windows startup.");
}
