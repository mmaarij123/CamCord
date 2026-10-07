#pragma once
#include <windows.h>
#include <string>

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    bool Start(const std::wstring& executable, const std::wstring& arguments, const std::wstring& logPath, bool interactive);
    bool SendQuitAndWait(DWORD timeoutMs = 15000);
    DWORD Wait(DWORD timeoutMs = INFINITE);
    DWORD ExitCode() const;
    bool Running() const;
    void Close();
    static DWORD Run(const std::wstring& executable, const std::wstring& arguments, const std::wstring& logPath, DWORD timeoutMs = 30000);
private:
    PROCESS_INFORMATION pi_{};
    HANDLE stdinWrite_ = nullptr;
};

std::wstring QuoteArg(const std::wstring& value);
std::wstring FindFfmpeg();
