#include "Process.h"
#include <filesystem>
#include <vector>

std::wstring QuoteArg(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out += c; slashes = 0; continue; }
        out.append(slashes, L'\\'); slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); out += L'\"';
    return out;
}

std::wstring FindFfmpeg() {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    auto adjacent = std::filesystem::path(module).parent_path() / L"ffmpeg.exe";
    if (std::filesystem::exists(adjacent)) return adjacent.wstring();
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, found, nullptr)) return found;
    return L"";
}

ChildProcess::~ChildProcess() { if (Running()) SendQuitAndWait(); Close(); }

bool ChildProcess::Start(const std::wstring& executable, const std::wstring& arguments, const std::wstring& logPath, bool interactive) {
    if (Running()) return false;
    Close();
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return false;
    HANDLE stdinRead = nullptr;
    HANDLE nullInput = nullptr;
    if (interactive) {
        if (!CreatePipe(&stdinRead, &stdinWrite_, &sa, 0)) { CloseHandle(log); return false; }
        if (!SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(stdinRead); CloseHandle(stdinWrite_); stdinWrite_ = nullptr; CloseHandle(log); return false;
        }
    } else {
        nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nullInput == INVALID_HANDLE_VALUE) { CloseHandle(log); return false; }
    }
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = interactive ? stdinRead : nullInput;
    si.StartupInfo.hStdOutput = log; si.StartupInfo.hStdError = log;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<BYTE> attributes(attributeBytes);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    const bool initialized = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attributeBytes) != FALSE;
    HANDLE inherited[] = { si.StartupInfo.hStdInput, log };
    const bool restricted = initialized && UpdateProcThreadAttribute(si.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
    std::wstring cmd = QuoteArg(executable) + L" " + arguments;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end()); mutableCmd.push_back(L'\0');
    const BOOL ok = restricted && CreateProcessW(executable.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi_);
    if (initialized) DeleteProcThreadAttributeList(si.lpAttributeList);
    if (stdinRead) CloseHandle(stdinRead);
    if (nullInput && nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
    CloseHandle(log);
    if (!ok) { if (stdinWrite_) { CloseHandle(stdinWrite_); stdinWrite_ = nullptr; } ZeroMemory(&pi_, sizeof(pi_)); }
    return ok == TRUE;
}

bool ChildProcess::SendQuitAndWait(DWORD timeoutMs) {
    if (!pi_.hProcess) return true;
    if (stdinWrite_) {
        DWORD written = 0; const char quit[] = "q\n";
        WriteFile(stdinWrite_, quit, sizeof(quit) - 1, &written, nullptr);
        CloseHandle(stdinWrite_); stdinWrite_ = nullptr;
    }
    DWORD result = WaitForSingleObject(pi_.hProcess, timeoutMs);
    if (result != WAIT_OBJECT_0) {
        TerminateProcess(pi_.hProcess, 2);
        WaitForSingleObject(pi_.hProcess, 3000);
        return false;
    }
    return ExitCode() == 0;
}

DWORD ChildProcess::Wait(DWORD timeoutMs) { return pi_.hProcess ? WaitForSingleObject(pi_.hProcess, timeoutMs) : WAIT_FAILED; }
DWORD ChildProcess::ExitCode() const { DWORD c = ERROR_INVALID_HANDLE; if (pi_.hProcess) GetExitCodeProcess(pi_.hProcess, &c); return c; }
bool ChildProcess::Running() const { return pi_.hProcess && WaitForSingleObject(pi_.hProcess, 0) == WAIT_TIMEOUT; }
void ChildProcess::Close() {
    if (stdinWrite_) { CloseHandle(stdinWrite_); stdinWrite_ = nullptr; }
    if (pi_.hThread) CloseHandle(pi_.hThread);
    if (pi_.hProcess) CloseHandle(pi_.hProcess);
    ZeroMemory(&pi_, sizeof(pi_));
}
DWORD ChildProcess::Run(const std::wstring& executable, const std::wstring& arguments, const std::wstring& logPath, DWORD timeoutMs) {
    ChildProcess p; if (!p.Start(executable, arguments, logPath, false)) return ERROR_FILE_NOT_FOUND;
    const auto wait = p.Wait(timeoutMs);
    if (wait != WAIT_OBJECT_0) { p.SendQuitAndWait(1000); return wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : ERROR_GEN_FAILURE; }
    return p.ExitCode();
}
