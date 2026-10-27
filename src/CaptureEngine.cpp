#include "CaptureEngine.h"
#include "VideoEncoding.h"
#include "CaptureSources.h"
#include <atomic>
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

struct CaptureEngine::StreamBridge {
    ChildProcess producer;
    HANDLE input = nullptr;
    HANDLE wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    std::thread reader, writer;
    std::atomic<bool> stopping{false}, failed{false}, cancelWrite{false};
    std::atomic<ULONGLONG> writeStarted{0};
    std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<std::vector<BYTE>> latest;
    // Three pending frames absorb normal producer/encoder jitter. Keep memory
    // bounded, including one reader and one in-flight encoder frame.
    std::deque<std::shared_ptr<std::vector<BYTE>>> pending;
    std::array<std::shared_ptr<std::vector<BYTE>>, 5> frames;
    void Stop(DWORD writerTimeout = 30000) {
        stopping = true; changed.notify_all();
        if (wake) SetEvent(wake);
        // Producer owns no encoded video: cancelling its frame wait cannot lose
        // buffered MP4 data. The encoder is separately drained by pipe EOF.
        producer.StopRawProducer();
        if (reader.joinable()) reader.join();
        const auto began = writeStarted.load();
        if (began && GetTickCount64() - began >= 30000) writerTimeout = 0;
        if (!JoinInputWriter(writer, cancelWrite, writerTimeout)) failed = true;
        if (input) { CloseHandle(input); input = nullptr; }
    }
    ~StreamBridge() { Stop(); if (timer) CloseHandle(timer); if (wake) CloseHandle(wake); }
};

CaptureEngine::CaptureEngine() = default;
CaptureEngine::~CaptureEngine() { if (bridge_) Stop(); }
bool CaptureEngine::Running() const {
    if (!process_.Running() || !bridge_ || bridge_->failed) return false;
    const auto began = bridge_->writeStarted.load();
    return !began || GetTickCount64() - began < 30000;
}

OperationResult CaptureEngine::Start(const std::wstring& ffmpeg, const std::wstring& output, const std::wstring& log,
    const RecordingSettings& settings, const EncoderChoice& encoder) {
    if (process_.Running()) return OperationResult::Failure(L"A recording is already running.");
    if (settings.width < 16 || settings.height < 16 || settings.width > 7680 || settings.height > 4320 ||
        settings.width % 2 || settings.height % 2 || settings.fps <= 0 || settings.fps > 120)
        return OperationResult::Failure(L"Unsupported recording size or frame rate.");
    bridge_.reset();
    auto target = settings.captureTarget;
    const auto available = ResolveCaptureTarget(target);
    if (!available.ok) return available;
    const auto sourceArgs = L"-hide_banner -loglevel warning -f lavfi -i "
        + QuoteArg(CaptureInputFilter(target, settings.fps, settings.width, settings.height))
        + L" -an -vf hwdownload,format=bgra,format=yuv420p -c:v rawvideo -threads 1 -fps_mode passthrough -pix_fmt yuv420p -f rawvideo pipe:1";
    return StartStream(ffmpeg, output, log, settings, encoder, sourceArgs);
}

OperationResult CaptureEngine::StartStream(const std::wstring& ffmpeg, const std::wstring& output, const std::wstring& log,
    const RecordingSettings& settings, const EncoderChoice& encoder, const std::wstring& sourceArgs) {
    // H.264 already uses 4:2:0. This cuts each pipe transfer and reusable frame
    // buffer by 62.5% versus BGRA, without changing output color depth.
    const auto frameBytes = static_cast<DWORD>(settings.width * settings.height * 3 / 2);
    bridge_ = std::make_unique<StreamBridge>();
    auto stream = bridge_.get();
    if (!stream->wake || !stream->timer) { bridge_.reset(); return OperationResult::Failure(L"The recording timer could not be created."); }
    for (auto& frame : stream->frames) frame = std::make_shared<std::vector<BYTE>>(frameBytes);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outputPipe = nullptr;
    if (!CreatePipe(&stream->input, &outputPipe, &security, 1024 * 1024)) { bridge_.reset(); return OperationResult::Failure(L"Capture pipe could not be created."); }
    if (!SetHandleInformation(stream->input, HANDLE_FLAG_INHERIT, 0)) { CloseHandle(outputPipe); bridge_.reset(); return OperationResult::Failure(L"Capture pipe could not be secured."); }
    const bool launched = stream->producer.Start(ffmpeg, sourceArgs, log + L".source", false, outputPipe);
    CloseHandle(outputPipe);
    if (!launched) { bridge_.reset(); return OperationResult::Failure(L"The selected source could not be started."); }
    stream->reader = std::thread([stream, frameBytes] {
        try {
            while (!stream->stopping) {
                std::shared_ptr<std::vector<BYTE>> frame;
                {
                    std::lock_guard lock(stream->mutex);
                    for (const auto& slot : stream->frames) if (slot.use_count() == 1) { frame = slot; break; }
                }
                if (!frame) { stream->failed = true; break; }
                DWORD received = 0;
                while (received < frameBytes && !stream->stopping) {
                    DWORD count = 0;
                    if (!ReadFile(stream->input, frame->data() + received, frameBytes - received, &count, nullptr) || !count) break;
                    received += count;
                }
                if (received != frameBytes) { if (!stream->stopping) stream->failed = true; break; }
                {
                    std::lock_guard lock(stream->mutex);
                    stream->latest = std::move(frame);
                    stream->pending.push_back(stream->latest);
                    if (stream->pending.size() > 3) stream->pending.pop_front();
                }
                stream->changed.notify_one();
            }
        } catch (...) { stream->failed = true; }
        stream->changed.notify_all();
        SetEvent(stream->wake);
    });
    {
        std::unique_lock lock(stream->mutex);
        stream->changed.wait_for(lock, std::chrono::seconds(5), [stream] { return stream->latest || stream->failed; });
        if (!stream->latest || stream->failed) {
            lock.unlock(); bridge_.reset();
            return OperationResult::Failure(L"No frames arrived from the selected source. Restore it or choose another source. Protected apps may not allow capture.");
        }
        // A single frame of look-ahead prevents independent capture/encoder
        // clocks from repeatedly sampling just before a new frame arrives.
        stream->changed.wait_for(lock, std::chrono::nanoseconds(1000000000ll / settings.fps),
            [stream] { return stream->pending.size() >= 2 || stream->failed; });
    }
    const auto dimensions = std::to_wstring(settings.width) + L"x" + std::to_wstring(settings.height);
    const auto encoderArgs = L"-y -hide_banner -loglevel warning -probesize 32 -analyzeduration 0 -f rawvideo -pixel_format yuv420p -video_size " + dimensions
        + L" -framerate " + std::to_wstring(settings.fps) + L" -i pipe:0" + VideoEncodingArguments(settings, encoder)
        + L" -movflags +frag_keyframe+empty_moov+default_base_moof -flush_packets 1 " + QuoteArg(output);
    if (!process_.Start(ffmpeg, encoderArgs, log, true, nullptr, true)) { bridge_.reset(); return OperationResult::Failure(L"The video encoder could not be started."); }
    startedAt_ = std::chrono::steady_clock::now();
    stream->writer = std::thread([this, stream, frameBytes, fps = settings.fps] {
        try {
            const auto interval = std::chrono::nanoseconds(1000000000ll / fps);
            auto next = std::chrono::steady_clock::now();
            while (!stream->stopping && !stream->failed) {
                std::shared_ptr<std::vector<BYTE>> frame;
                {
                    std::lock_guard lock(stream->mutex);
                    if (!stream->pending.empty()) { frame = stream->pending.front(); stream->pending.pop_front(); }
                    else frame = stream->latest; // Static windows still need a complete recording timeline.
                }
                stream->writeStarted = GetTickCount64();
                if (frame && !process_.WriteInput(frame->data(), frameBytes, &stream->cancelWrite)) { stream->failed = true; break; }
                stream->writeStarted = 0;
                next += interval;
                const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(next - std::chrono::steady_clock::now()).count();
                if (remaining > 0) {
                    LARGE_INTEGER due{}; due.QuadPart = -(remaining + 99) / 100;
                    if (!SetWaitableTimer(stream->timer, &due, 0, nullptr, nullptr, FALSE)) { stream->failed = true; break; }
                    const HANDLE handles[] = {stream->wake, stream->timer};
                    const auto waited = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                    if (waited == WAIT_FAILED) stream->failed = true;
                    if (waited != WAIT_OBJECT_0 + 1) break;
                }
            }
        } catch (...) { stream->failed = true; }
        stream->changed.notify_all();
    });
    Sleep(700);
    if (!Running()) { Stop(); return OperationResult::Failure(L"The selected source or video encoder stopped during startup. See the session log."); }
    return OperationResult::Success();
}

OperationResult CaptureEngine::Stop(DWORD timeoutMs) {
    const bool wasAlive = process_.Running();
    const auto beforeStop = GetTickCount64();
    if (bridge_) bridge_->Stop(timeoutMs == INFINITE ? 30000 : timeoutMs);
    const bool cancelledWrite = bridge_ && bridge_->cancelWrite.load();
    if (timeoutMs != INFINITE) {
        const auto elapsed = GetTickCount64() - beforeStop;
        timeoutMs = elapsed >= timeoutMs ? 0 : timeoutMs - static_cast<DWORD>(elapsed);
    }
    // A cancelled frame means this encoder stopped consuming input. Do not
    // wait forever on that failed process; healthy encoders still drain fully.
    const bool clean = process_.EndInputAndWait(cancelledWrite && timeoutMs == INFINITE ? 3000 : timeoutMs);
    process_.Close(); bridge_.reset();
    return wasAlive && clean && !cancelledWrite ? OperationResult::Success() : OperationResult::Failure(L"Recording stopped unexpectedly; recovery files were retained.");
}
