#include "AudioCaptureEngine.h"
#include "ComPtr.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstdint>

namespace {
// RF64 uses 64-bit sizes in ds64, so a long PCM capture cannot wrap at 4 GiB.
class WaveFile {
public:
    bool Open(const std::wstring& path, const WAVEFORMATEX* format) {
        stream_.open(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!stream_) return false;
        blockAlign_ = format->nBlockAlign;
        const UINT32 formatBytes = sizeof(WAVEFORMATEX) + format->cbSize;
        stream_.write("RF64", 4); Write32(UINT32_MAX); stream_.write("WAVEds64", 8); Write32(28);
        Write64(0); Write64(0); Write64(0); Write32(0);
        stream_.write("fmt ", 4); Write32(formatBytes);
        stream_.write(reinterpret_cast<const char*>(format), formatBytes);
        if (formatBytes & 1) stream_.put(0);
        stream_.write("data", 4); Write32(UINT32_MAX);
        return stream_.good();
    }
    bool Write(const BYTE* data, UINT32 bytes) {
        stream_.write(reinterpret_cast<const char*>(data), bytes);
        if (!stream_) { failed_ = true; return false; }
        dataBytes_ += bytes;
        return true;
    }
    bool Silence(uint64_t bytes) {
        static const BYTE zeros[8192]{};
        while (bytes) {
            const auto count = static_cast<UINT32>(std::min<uint64_t>(bytes, sizeof(zeros)));
            if (!Write(zeros, count)) return false;
            bytes -= count;
        }
        return true;
    }
    bool Close() {
        if (closed_) return !failed_;
        closed_ = true;
        if (!stream_.is_open()) return false;
        if (!stream_) failed_ = true;
        stream_.clear();
        if (dataBytes_ & 1) stream_.put(0);
        const auto end = stream_.tellp();
        stream_.seekp(20);
        Write64(end >= 8 ? static_cast<uint64_t>(end) - 8 : 0);
        Write64(dataBytes_); Write64(blockAlign_ ? dataBytes_ / blockAlign_ : 0);
        stream_.flush();
        if (!stream_) failed_ = true;
        stream_.close();
        if (!stream_) failed_ = true;
        return !failed_;
    }
    ~WaveFile() { Close(); }
private:
    void Write32(UINT32 n) { stream_.write(reinterpret_cast<const char*>(&n), 4); }
    void Write64(uint64_t n) { stream_.write(reinterpret_cast<const char*>(&n), 8); }
    std::ofstream stream_;
    uint64_t dataBytes_ = 0;
    UINT32 blockAlign_ = 0;
    bool closed_ = false, failed_ = false;
};

uint64_t Qpc100ns() {
    LARGE_INTEGER counter{}, frequency{};
    QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    return static_cast<uint64_t>(counter.QuadPart / frequency.QuadPart) * 10000000ull
        + static_cast<uint64_t>((counter.QuadPart % frequency.QuadPart) * 10000000ull / frequency.QuadPart);
}

uint64_t TimelineFrame(uint64_t packet100ns, uint64_t start100ns, UINT32 sampleRate) {
    if (packet100ns <= start100ns) return 0;
    const auto ticks = packet100ns - start100ns;
    return (ticks / 10000000ull) * sampleRate + (ticks % 10000000ull) * sampleRate / 10000000ull;
}
}

AudioCaptureEngine::~AudioCaptureEngine() { Stop(); }

std::wstring AudioCaptureEngine::LastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_); return lastError_;
}

void AudioCaptureEngine::SetError(const std::wstring& message) {
    { std::lock_guard<std::mutex> lock(errorMutex_); lastError_ = message; }
    failed_ = true;
}

OperationResult AudioCaptureEngine::Start(bool loopback, const std::wstring& wavPath) {
    Stop(); stop_ = false; started_ = false; capturedAudio_ = false; finished_ = false; failed_ = false;
    { std::lock_guard<std::mutex> lock(errorMutex_); lastError_.clear(); }
    try { thread_ = std::thread(&AudioCaptureEngine::CaptureThread, this, loopback, wavPath); }
    catch (const std::exception&) { return OperationResult::Failure(L"An audio capture thread could not be started."); }
    for (int i = 0; i < 200 && !started_ && !finished_; ++i) Sleep(10);
    if (!started_ || failed_) {
        Stop(); const auto error = LastError();
        return OperationResult::Failure(error.empty() ? L"Audio device did not start in time." : error);
    }
    return OperationResult::Success();
}

void AudioCaptureEngine::Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    started_ = false;
}

void AudioCaptureEngine::CaptureThread(bool loopback, std::wstring path) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> device; ComPtr<IAudioClient> client; ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX* format = nullptr; HANDLE mmcss = nullptr; DWORD task = 0;
    bool clientStarted = false;
    try {
        do {
            if (FAILED(com)) { SetError(L"The audio thread could not initialize Windows audio services."); break; }
            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put())))) { SetError(L"Windows audio service is unavailable."); break; }
            const EDataFlow flow = loopback ? eRender : eCapture;
            if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, device.put()))) { SetError(loopback ? L"No system playback device is available." : L"No default microphone is available."); break; }
            if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.put())))) { SetError(L"The audio device could not be opened."); break; }
            if (FAILED(client->GetMixFormat(&format)) || !format || !format->nBlockAlign || !format->nSamplesPerSec) { SetError(L"The audio device format could not be read."); break; }
            const DWORD flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
            if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 10000000, 0, format, nullptr))) { SetError(L"Audio capture initialization failed."); break; }
            if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.put())))) { SetError(L"Audio capture service is unavailable."); break; }
            WaveFile wave;
            if (!wave.Open(path, format)) { SetError(L"The temporary audio file could not be written."); break; }
            mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &task);
            const auto origin = Qpc100ns(); startedAt_ = std::chrono::steady_clock::now();
            if (FAILED(client->Start())) { SetError(L"The audio device could not start."); break; }
            clientStarted = true; started_ = true;
            uint64_t writtenFrames = 0, previousDeviceEnd = 0;
            bool haveDevicePosition = false;
            auto writeGap = [&](uint64_t target) {
                if (target <= writtenFrames) return true;
                if (!wave.Silence((target - writtenFrames) * format->nBlockAlign)) return false;
                writtenFrames = target; return true;
            };
            while (!stop_ && !failed_) {
                UINT32 packets = 0;
                if (FAILED(capture->GetNextPacketSize(&packets))) { SetError(L"An audio device was disconnected during recording."); break; }
                if (!packets) { Sleep(4); continue; }
                while (packets && !stop_ && !failed_) {
                    BYTE* data = nullptr; UINT32 frames = 0; DWORD bufferFlags = 0;
                    UINT64 devicePosition = 0, packetQpc = 0;
                    const HRESULT hr = capture->GetBuffer(&data, &frames, &bufferFlags, &devicePosition, &packetQpc);
                    if (FAILED(hr)) { SetError(L"Audio capture stopped unexpectedly."); break; }
                    // WASAPI can omit packets for long silent intervals. Preserve their position
                    // on the capture clock instead of joining later speech to the start of the file.
                    uint64_t target = writtenFrames;
                    if (!(bufferFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) target = TimelineFrame(packetQpc, origin, format->nSamplesPerSec);
                    else if (haveDevicePosition && devicePosition > previousDeviceEnd) target += devicePosition - previousDeviceEnd;
                    bool ok = true;
                    if (target > writtenFrames + format->nSamplesPerSec / 500) ok = writeGap(target);
                    const UINT32 bytes = frames * format->nBlockAlign;
                    if (ok) ok = (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) ? wave.Silence(bytes) : (data && wave.Write(data, bytes));
                    if (ok) { writtenFrames += frames; if (frames) capturedAudio_ = true; }
                    const auto released = capture->ReleaseBuffer(frames);
                    previousDeviceEnd = devicePosition + frames; haveDevicePosition = true;
                    if (!ok) { SetError(L"Writing recorded audio failed. Check available disk space and the recording drive."); break; }
                    if (FAILED(released) || FAILED(capture->GetNextPacketSize(&packets))) { SetError(L"Audio capture stopped unexpectedly."); break; }
                }
            }
            if (FAILED(client->Stop()) && !failed_) SetError(L"The audio device could not stop cleanly.");
            clientStarted = false;
            if (!failed_ && !writeGap(TimelineFrame(Qpc100ns(), origin, format->nSamplesPerSec))) SetError(L"Writing recorded audio failed. Check the recording drive.");
            if (writtenFrames) capturedAudio_ = true;
            if (!wave.Close()) SetError(L"The audio file could not be finalized. Check available disk space.");
        } while (false);
    } catch (const std::exception&) { SetError(L"An audio recording file could not be accessed or written."); }
    if (clientStarted) client->Stop();
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    if (format) CoTaskMemFree(format);
    capture.reset(); client.reset(); device.reset(); enumerator.reset();
    if (SUCCEEDED(com)) CoUninitialize();
    finished_ = true;
}
