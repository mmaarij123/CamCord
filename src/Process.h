#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <thread>

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    bool Start(const std::wstring& executable, const std::wstring& arguments, const std::wstring& logPath, bool interactive,
        HANDLE outputPipe = nullptr, bool rawInput = false);
    bool WriteInput(const void* bytes, DWORD length, const std::atomic<bool>* cancelled = nullptr);
    bool EndInputAndWait(DWORD timeoutMs = INFINITE);
    void StopRawProducer(); // Only for an unencoded capture producer, never a video encoder.
    // Capture shutdown drains the encoder on a background worker. A fixed time
    // limit would kill valid long recordings before their final frames flush.
    bool SendQuitAndWait(DWORD timeoutMs = INFINITE);
    DWORD Wait(DWORD timeoutMs = INFINITE);
    DWORD ExitCode() const;
    bool Running() const;
    void Close();
    static DWORD Run(const std::wstring& executable, const std::wstring& arguments, const std::wstring& logPath, DWORD timeoutMs = 30000);
private:
    PROCESS_INFORMATION pi_{};
    HANDLE stdinWrite_ = nullptr;
    bool rawInput_ = false;
};

std::wstring QuoteArg(const std::wstring& value);
std::wstring FindFfmpeg();
// Finish an in-flight raw frame normally; cancel a blocked pipe writer only
// after the deadline. This does not impose a limit on encoded-output draining.
bool JoinInputWriter(std::thread& writer, std::atomic<bool>& cancelled, DWORD timeoutMs = 30000);
