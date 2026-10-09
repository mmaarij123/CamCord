#include "RecordingManager.h"
#include "Process.h"
#include "RecordingQuality.h"
#include <filesystem>
#include <fstream>
#include <sstream>

RecordingManager::RecordingManager() = default;

OperationResult RecordingManager::Failure(const std::wstring& message) const {
    return OperationResult::Failure(message + (sessionFolder_.empty() ? L"" : L"\nRecovery files: " + sessionFolder_));
}

OperationResult RecordingManager::Initialize() {
    try {
        auto folder = output_.EnsureCaptureFolder(); if (!folder.ok) return folder;
        ffmpeg_ = FindFfmpeg();
        if (ffmpeg_.empty()) return OperationResult::Failure(L"ffmpeg.exe was not found. Put it beside CamCord.exe or install it on PATH.");
        return OperationResult::Success();
    } catch (const std::exception&) { return Failure(L"The recording folder could not be accessed."); }
}

OperationResult RecordingManager::Start(RecordingSettings& settings) {
    if (state_ != RecorderState::Idle) return OperationResult::Failure(L"A recording is already active.");
    settings.bitrateMbps = NormalizeBitrateMbps(settings.bitrateMbps);
    try {
        auto ready = Initialize(); if (!ready.ok) return ready;
        auto space = output_.EnsureFreeSpace(512ull * 1024ull * 1024ull); if (!space.ok) return space;
        sessionFolder_ = output_.NewSessionFolder();
        if (sessionFolder_.empty()) return OperationResult::Failure(L"A temporary recording folder could not be created.");
        segments_.clear(); segmentFailure_.clear(); accumulated_ = {};
        encoder_ = detector_.Detect(ffmpeg_, sessionFolder_);
        if (encoder_.name.empty()) return Failure(L"No working H.264 encoder is available in this FFmpeg build.");
        if (settings.fps == 120 && (!encoder_.hardware || !detector_.Supports(ffmpeg_, sessionFolder_, encoder_, settings))) settings.fps = 60;
        settings_ = settings; outputPath_ = output_.NewOutputPath();
        auto result = StartSegment();
        if (!result.ok && encoder_.hardware) {
            encoder_ = { L"libx264", L"Software H.264", false };
            if (settings_.fps == 120) { settings_.fps = 60; settings.fps = 60; }
            result = StartSegment();
        }
        if (!result.ok) state_ = RecorderState::Idle;
        return result.ok ? OperationResult::Success(L"Recording with " + encoder_.label) : result;
    } catch (const std::exception&) {
        if (capture_) capture_->Stop();
        if (systemAudio_) systemAudio_->Stop();
        if (microphone_) microphone_->Stop();
        capture_.reset(); systemAudio_.reset(); microphone_.reset();
        state_ = RecorderState::Idle;
        return Failure(L"The recording could not be started. Check the folder and available disk space.");
    }
}

OperationResult RecordingManager::StartSegment() {
    Segment seg;
    const auto base = std::filesystem::path(sessionFolder_) / (L"segment_" + std::to_wstring(segments_.size() + 1));
    seg.video = base.wstring() + L"_video.mp4"; seg.systemAudio = base.wstring() + L"_system.wav"; seg.microphone = base.wstring() + L"_mic.wav";
    seg.muxed = base.wstring() + L"_muxed.mp4"; seg.log = base.wstring() + L".log";
    capture_ = std::make_unique<CaptureEngine>(); systemAudio_ = std::make_unique<AudioCaptureEngine>(); microphone_ = std::make_unique<AudioCaptureEngine>();
    if (settings_.systemAudio) {
        auto result = systemAudio_->Start(true, seg.systemAudio);
        if (!result.ok) { systemAudio_.reset(); return Failure(result.message); }
    }
    if (settings_.microphone) {
        auto result = microphone_->Start(false, seg.microphone);
        if (!result.ok) { systemAudio_->Stop(); systemAudio_.reset(); microphone_.reset(); return Failure(result.message); }
    }
    const auto videoLaunch = std::chrono::steady_clock::now();
    if (settings_.systemAudio) seg.systemTrimMs = std::chrono::duration_cast<std::chrono::milliseconds>(videoLaunch - systemAudio_->StartedAt()).count();
    if (settings_.microphone) seg.micTrimMs = std::chrono::duration_cast<std::chrono::milliseconds>(videoLaunch - microphone_->StartedAt()).count();
    auto video = capture_->Start(ffmpeg_, seg.video, seg.log, settings_, encoder_);
    if (!video.ok) { systemAudio_->Stop(); microphone_->Stop(); capture_.reset(); return Failure(video.message); }
    segments_.push_back(seg); segmentStarted_ = videoLaunch; state_ = RecorderState::Recording;
    return OperationResult::Success();
}

OperationResult RecordingManager::StopSegment() {
    if (state_ != RecorderState::Recording) return OperationResult::Success();
    accumulated_ += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - segmentStarted_);
    // Stop the video first, so audio covers all frames drained by the encoder.
    auto result = capture_ ? capture_->Stop() : OperationResult::Failure(L"Capture was not active.");
    auto stopAudio = [&](std::unique_ptr<AudioCaptureEngine>& audio, bool enabled, bool& hasAudio) {
        if (!audio) return;
        audio->Stop(); hasAudio = audio->CapturedAudio();
        if (enabled && !audio->LastError().empty()) result = OperationResult::Failure(audio->LastError());
    };
    stopAudio(systemAudio_, settings_.systemAudio, segments_.back().hasSystem);
    stopAudio(microphone_, settings_.microphone, segments_.back().hasMic);
    capture_.reset(); systemAudio_.reset(); microphone_.reset();
    if (!result.ok) segmentFailure_ = result.message;
    return result.ok ? result : Failure(result.message);
}

OperationResult RecordingManager::Pause() {
    if (state_ != RecorderState::Recording) return OperationResult::Failure(L"No active recording can be paused.");
    auto result = StopSegment(); state_ = RecorderState::Paused;
    return result.ok ? OperationResult::Success(L"Paused") : result;
}

OperationResult RecordingManager::Resume() {
    if (state_ != RecorderState::Paused) return OperationResult::Failure(L"Recording is not paused.");
    if (!segmentFailure_.empty()) return Failure(segmentFailure_ + L" Stop this session before recording again.");
    try { return StartSegment(); }
    catch (const std::exception&) {
        if (capture_) capture_->Stop();
        if (systemAudio_) systemAudio_->Stop();
        if (microphone_) microphone_->Stop();
        capture_.reset(); systemAudio_.reset(); microphone_.reset();
        return Failure(L"The next recording segment could not be started.");
    }
}

OperationResult RecordingManager::RunFfmpeg(const std::wstring& args, const std::wstring& log, DWORD timeout) {
    const DWORD code = ChildProcess::Run(ffmpeg_, args, log, timeout);
    return code == 0 ? OperationResult::Success() : Failure(L"FFmpeg could not validate or finalize this recording. No recovery files were deleted. Log: " + log);
}

OperationResult RecordingManager::Finalize() {
    if (segments_.empty()) return Failure(L"No video segment was produced.");
    std::vector<std::wstring> ready;
    for (auto& segment : segments_) {
        if (!std::filesystem::exists(segment.video) || std::filesystem::file_size(segment.video) == 0)
            return Failure(L"A video segment is missing or empty. The session was not saved as a complete recording.");
        // Decode validation catches corrupt/truncated data that a stream-copy remux can overlook.
        auto validate = RunFfmpeg(L"-hide_banner -loglevel error -xerror -err_detect explode -i " + QuoteArg(segment.video)
            + L" -map 0:v:0 -an -abort_on empty_output -f null NUL", segment.log + L".validate.log");
        if (!validate.ok) return validate;
        std::vector<std::pair<std::wstring, long long>> audios;
        if (segment.hasSystem) audios.emplace_back(segment.systemAudio, segment.systemTrimMs);
        if (segment.hasMic) audios.emplace_back(segment.microphone, segment.micTrimMs);
        std::wstring args = L"-y -hide_banner -loglevel error -xerror -i " + QuoteArg(segment.video);
        for (const auto& audio : audios) {
            if (audio.second > 0) args += L" -ss " + std::to_wstring(audio.second) + L"ms";
            args += L" -i " + QuoteArg(audio.first);
        }
        if (settings_.systemAudio || settings_.microphone) {
            // Every paused segment must have exactly the same stream layout, including silent ones.
            if (audios.empty()) args += L" -f lavfi -i anullsrc=r=48000:cl=stereo";
            if (audios.size() < 2) {
                args += L" -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 192k -ar 48000 -ac 2 -af "
                    + QuoteArg(L"aresample=48000:async=1:first_pts=0,apad") + L" -shortest";
            } else {
                args += L" -filter_complex " + QuoteArg(L"[1:a]aresample=48000:async=1:first_pts=0[a1];[2:a]aresample=48000:async=1:first_pts=0[a2];[a1][a2]amix=inputs=2:duration=longest:dropout_transition=2,apad[a]")
                    + L" -map 0:v:0 -map " + QuoteArg(L"[a]") + L" -c:v copy -c:a aac -b:a 192k -ar 48000 -ac 2 -shortest";
            }
        } else args += L" -map 0:v:0 -c:v copy -an";
        args += L" -abort_on empty_output " + QuoteArg(segment.muxed);
        auto mux = RunFfmpeg(args, segment.log + L".mux.log"); if (!mux.ok) return mux;
        ready.push_back(segment.muxed);
    }
    const auto listPath = (std::filesystem::path(sessionFolder_) / L"concat.txt").wstring();
    std::ofstream list(std::filesystem::path(listPath), std::ios::binary | std::ios::trunc);
    if (!list) return Failure(L"The recording segment list could not be created.");
    for (const auto& path : ready) {
        const auto portable = std::filesystem::path(path).generic_wstring();
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, portable.c_str(), static_cast<int>(portable.size()), nullptr, 0, nullptr, nullptr);
        if (count <= 0) return Failure(L"A recording path could not be encoded.");
        std::string utf8(static_cast<size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, portable.c_str(), static_cast<int>(portable.size()), utf8.data(), count, nullptr, nullptr);
        size_t pos = 0; while ((pos = utf8.find('\'', pos)) != std::string::npos) { utf8.replace(pos, 1, "'\\''"); pos += 4; }
        list << "file '" << utf8 << "'\n";
    }
    list.close();
    if (!list) return Failure(L"The recording segment list could not be written. Check available disk space.");
    // Publish only after FFmpeg completes: a failed save never leaves a final-looking partial MP4.
    const auto staging = (std::filesystem::path(sessionFolder_) / L"finished.mp4").wstring();
    auto result = RunFfmpeg(L"-y -hide_banner -loglevel error -xerror -f concat -safe 0 -i " + QuoteArg(listPath)
        + L" -map 0 -c copy -abort_on empty_output -movflags +faststart " + QuoteArg(staging),
        (std::filesystem::path(sessionFolder_) / L"finalize.log").wstring());
    if (!result.ok) return result;
    if (!std::filesystem::exists(staging) || std::filesystem::file_size(staging) == 0) return Failure(L"The finished recording is empty.");
    if (!MoveFileExW(staging.c_str(), outputPath_.c_str(), MOVEFILE_WRITE_THROUGH))
        return Failure(L"The finished recording could not be moved to the selected folder. It remains in the recovery folder.");
    lastOutput_ = outputPath_;
    return OperationResult::Success(lastOutput_);
}

OperationResult RecordingManager::Stop() {
    if (state_ != RecorderState::Recording && state_ != RecorderState::Paused) return OperationResult::Failure(L"No recording is active.");
    OperationResult result;
    try {
        if (state_ == RecorderState::Recording) StopSegment();
        state_ = RecorderState::Saving;
        result = segmentFailure_.empty() ? Finalize() : Failure(segmentFailure_);
        if (result.ok) Cleanup();
    } catch (const std::exception&) { result = Failure(L"Saving failed because a recording file could not be accessed or written."); }
    state_ = RecorderState::Idle;
    return result;
}

void RecordingManager::Cleanup() {
    if (!sessionFolder_.empty()) { std::error_code ec; std::filesystem::remove_all(sessionFolder_, ec); }
    segments_.clear(); sessionFolder_.clear();
}

bool RecordingManager::CaptureAlive() const {
    return state_ != RecorderState::Recording || (capture_ && capture_->Running()
        && (!settings_.systemAudio || (systemAudio_ && systemAudio_->Healthy()))
        && (!settings_.microphone || (microphone_ && microphone_->Healthy())));
}

unsigned long long RecordingManager::ElapsedSeconds() const {
    auto total = accumulated_;
    if (state_ == RecorderState::Recording) total += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - segmentStarted_);
    return static_cast<unsigned long long>(total.count() / 1000);
}
