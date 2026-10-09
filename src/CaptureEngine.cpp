#include "CaptureEngine.h"
#include "VideoEncoding.h"
#include "CaptureSources.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

struct CaptureEngine::StreamBridge {
    ChildProcess producer;
    HANDLE input = nullptr;
    std::thread reader, writer;
    std::atomic<bool> stopping{false}, failed{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<std::vector<BYTE>> latest;
    void Stop() {
        stopping = true; changed.notify_all();
        // Producer owns no encoded video: cancelling its frame wait cannot lose
        // buffered MP4 data. The encoder is separately drained by pipe EOF.
        producer.StopRawProducer();
        if (reader.joinable()) reader.join();
        if (writer.joinable()) writer.join();
        if (input) { CloseHandle(input); input = nullptr; }
    }
    ~StreamBridge() { Stop(); }
};

CaptureEngine::CaptureEngine() = default;
CaptureEngine::~CaptureEngine() { if (bridge_) { bridge_->Stop(); process_.EndInputAndWait(); process_.Close(); } }
bool CaptureEngine::Running() const { return process_.Running() && bridge_ && !bridge_->failed; }

OperationResult CaptureEngine::Start(const std::wstring& ffmpeg, const std::wstring& output, const std::wstring& log,
    const RecordingSettings& settings, const EncoderChoice& encoder) {
    if (process_.Running()) return OperationResult::Failure(L"A recording is already running.");
    if (settings.width < 16 || settings.height < 16 || settings.width > 7680 || settings.height > 4320 ||
        settings.fps <= 0 || settings.fps > 120) return OperationResult::Failure(L"Unsupported recording size or frame rate.");
    bridge_.reset();
    auto target = settings.captureTarget;
    const auto available = ResolveCaptureTarget(target);
    if (!available.ok) return available;
    const auto size = std::to_wstring(settings.width) + L":" + std::to_wstring(settings.height);
    const auto frameBytes = static_cast<DWORD>(settings.width * settings.height * 4);
    bridge_ = std::make_unique<StreamBridge>();
    auto stream = bridge_.get();
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outputPipe = nullptr;
    if (!CreatePipe(&stream->input, &outputPipe, &security, 0)) { bridge_.reset(); return OperationResult::Failure(L"Capture pipe could not be created."); }
    if (!SetHandleInformation(stream->input, HANDLE_FLAG_INHERIT, 0)) { CloseHandle(outputPipe); bridge_.reset(); return OperationResult::Failure(L"Capture pipe could not be secured."); }
    const auto filter = L"hwdownload,format=bgra,scale=" + size + L":force_original_aspect_ratio=decrease:flags=lanczos,pad="
        + size + L":(ow-iw)/2:(oh-ih)/2:color=black,format=bgra";
    const auto sourceArgs = L"-hide_banner -loglevel warning -f lavfi -i " + QuoteArg(CaptureInputFilter(target, settings.fps))
        + L" -an -vf " + QuoteArg(filter) + L" -c:v rawvideo -threads 1 -fps_mode passthrough -pix_fmt bgra -f rawvideo pipe:1";
    const bool launched = stream->producer.Start(ffmpeg, sourceArgs, log + L".source", false, outputPipe);
    CloseHandle(outputPipe);
    if (!launched) { bridge_.reset(); return OperationResult::Failure(L"The selected source could not be started."); }
    stream->reader = std::thread([stream, frameBytes] {
        try {
            while (!stream->stopping) {
                auto frame = std::make_shared<std::vector<BYTE>>(frameBytes);
                DWORD received = 0;
                while (received < frameBytes && !stream->stopping) {
                    DWORD count = 0;
                    if (!ReadFile(stream->input, frame->data() + received, frameBytes - received, &count, nullptr) || !count) break;
                    received += count;
                }
                if (received != frameBytes) { if (!stream->stopping) stream->failed = true; break; }
                { std::lock_guard lock(stream->mutex); stream->latest = std::move(frame); }
                stream->changed.notify_all();
            }
        } catch (...) { stream->failed = true; }
        stream->changed.notify_all();
    });
    {
        std::unique_lock lock(stream->mutex);
        stream->changed.wait_for(lock, std::chrono::seconds(5), [stream] { return stream->latest || stream->failed; });
        if (!stream->latest || stream->failed) {
            lock.unlock(); bridge_.reset();
            return OperationResult::Failure(L"No frames arrived from the selected source. Restore it or choose another source. Protected apps may not allow capture.");
        }
    }
    const auto dimensions = std::to_wstring(settings.width) + L"x" + std::to_wstring(settings.height);
    const auto encoderArgs = L"-y -hide_banner -loglevel warning -f rawvideo -pixel_format bgra -video_size " + dimensions
        + L" -framerate " + std::to_wstring(settings.fps) + L" -i pipe:0" + VideoEncodingArguments(settings, encoder)
        + L" -movflags +frag_keyframe+empty_moov+default_base_moof -flush_packets 1 " + QuoteArg(output);
    if (!process_.Start(ffmpeg, encoderArgs, log, true, nullptr, true)) { bridge_.reset(); return OperationResult::Failure(L"The video encoder could not be started."); }
    startedAt_ = std::chrono::steady_clock::now();
    stream->writer = std::thread([this, stream, frameBytes, fps = settings.fps] {
        const auto interval = std::chrono::nanoseconds(1000000000ll / fps);
        auto next = std::chrono::steady_clock::now();
        while (!stream->stopping && !stream->failed) {
            std::shared_ptr<std::vector<BYTE>> frame;
            { std::lock_guard lock(stream->mutex); frame = stream->latest; }
            if (frame && !process_.WriteInput(frame->data(), frameBytes)) { stream->failed = true; break; }
            next += interval;
            std::unique_lock lock(stream->mutex);
            stream->changed.wait_until(lock, next, [stream] { return stream->stopping.load() || stream->failed.load(); });
        }
    });
    Sleep(700);
    if (!Running()) { Stop(); return OperationResult::Failure(L"The selected source or video encoder stopped during startup. See the session log."); }
    return OperationResult::Success();
}

OperationResult CaptureEngine::Stop(DWORD timeoutMs) {
    const bool wasAlive = process_.Running();
    if (bridge_) bridge_->Stop();
    const bool clean = process_.EndInputAndWait(timeoutMs);
    process_.Close(); bridge_.reset();
    return wasAlive && clean ? OperationResult::Success() : OperationResult::Failure(L"Recording stopped unexpectedly; recovery files were retained.");
}
