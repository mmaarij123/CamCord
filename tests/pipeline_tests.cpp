// Safe integration tests: only generated color/sine media, never the desktop or microphone.
#include "../src/RecordingManager.h"
#include "../src/AudioCaptureEngine.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <cstring>

namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct PipelineTestAccess {
    static void Setup(RecordingManager& manager, const fs::path& session, const fs::path& output,
        const std::wstring& ffmpeg, bool audio) {
        fs::create_directories(session);
        manager.ffmpeg_ = ffmpeg; manager.sessionFolder_ = session.wstring(); manager.outputPath_ = output.wstring();
        manager.settings_.systemAudio = audio; manager.settings_.microphone = false;
        manager.state_ = RecorderState::Paused;
    }
    static void Add(RecordingManager& manager, const fs::path& video, const fs::path& audio = {}) {
        RecordingManager::Segment segment;
        segment.video = video.wstring(); segment.muxed = video.wstring() + L".muxed.mp4";
        segment.log = video.wstring() + L".log";
        segment.hasSystem = !audio.empty(); segment.systemAudio = audio.wstring();
        manager.segments_.push_back(segment);
    }
    static void Failure(RecordingManager& manager) { manager.segmentFailure_ = L"Synthetic capture stop failure"; }
};

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--slow-quit") {
        std::string command;
        std::getline(std::cin, command);
        Sleep(16000);
        return command == "q" ? 0 : 1;
    }
    if (argc != 3) { std::cerr << "Usage: pipeline_tests.exe ffmpeg.exe output-folder\n"; return 2; }
    const std::wstring ffmpeg = argv[1]; const fs::path root = fs::absolute(argv[2]);
    fs::create_directories(root);
    auto run = [&](const std::wstring& args, const std::wstring& label) {
        Check(ChildProcess::Run(ffmpeg, L"-y -hide_banner -loglevel error " + args,
            (root / (label + L".log")).wstring(), 30000) == 0, "Synthetic FFmpeg operation failed; inspect test logs");
    };
    auto video = [&](const fs::path& path) {
        run(L"-f lavfi -i color=c=blue:s=160x90:r=15 -t 2 -an -c:v libx264 -preset ultrafast -g 15 -pix_fmt yuv420p -movflags +frag_keyframe+empty_moov+default_base_moof " + QuoteArg(path.wstring()), path.filename().wstring());
    };
    try {
        {
            auto longDirectory = root / std::wstring(90, L'a') / std::wstring(90, L'b');
            while (longDirectory.wstring().size() <= MAX_PATH + 20) longDirectory /= L"extra-path-component";
            const fs::path extendedDirectory(L"\\\\?\\" + longDirectory.wstring());
            fs::create_directories(extendedDirectory);
            const auto engine = extendedDirectory / L"ffmpeg.exe";
            { std::ofstream marker(engine, std::ios::binary); marker << "test-only engine marker"; }
            const DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
            std::vector<wchar_t> originalPath(required ? required : 1);
            if (required) GetEnvironmentVariableW(L"PATH", originalPath.data(), static_cast<DWORD>(originalPath.size()));
            Check(SetEnvironmentVariableW(L"PATH", extendedDirectory.c_str()) != FALSE, "Could not set test-only engine search path");
            const auto discovered = FindFfmpeg();
            SetEnvironmentVariableW(L"PATH", required ? originalPath.data() : nullptr);
            Check(!discovered.empty() && fs::equivalent(discovered, engine), "Long engine search path was truncated");
            std::cout << "PASS: recording engine discovery preserves paths longer than MAX_PATH\n";
        }
        {
            std::vector<wchar_t> executable(32768);
            const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            Check(length && length < executable.size(), "Could not locate synthetic child process");
            ChildProcess delayed;
            Check(delayed.Start(std::wstring(executable.data(), length), L"--slow-quit",
                (root / L"slow-quit.log").wstring(), true), "Could not start synthetic delayed encoder");
            Check(delayed.SendQuitAndWait(), "A clean encoder shutdown was killed at the old 15-second limit");
            delayed.Close();
            std::cout << "PASS: encoder draining beyond 15 seconds completes without termination\n";
        }
        Check(TimelineFrame(120000000, 100000000, 48000) == 96000, "Leading audio silence calculation is wrong");
        Check(TimelineFrame(90000000, 100000000, 48000) == 0, "Pre-start audio timestamp underflowed");
        Check(TimelineFrame(100000000ull + 48ull * 3600 * 10000000, 100000000, 48000) == 48ull * 3600 * 48000, "Long capture timeline overflowed");
        const auto wavePath = root / L"rf64.wav";
        WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
        format.nSamplesPerSec = 48000; format.wBitsPerSample = 16; format.nBlockAlign = 4; format.nAvgBytesPerSec = 192000;
        {
            WaveFile wave; Check(wave.Open(wavePath.wstring(), &format), "RF64 open failed");
            Check(wave.Silence(192000), "RF64 silence write failed"); Check(wave.Close(), "RF64 close failed");
        }
        {
            std::ifstream file(wavePath, std::ios::binary); char magic[4]{}; file.read(magic, 4);
            Check(std::memcmp(magic, "RF64", 4) == 0, "Wrong RF64 marker");
            file.seekg(20); uint64_t riffBytes = 0, dataBytes = 0, samples = 0;
            file.read(reinterpret_cast<char*>(&riffBytes), 8); file.read(reinterpret_cast<char*>(&dataBytes), 8); file.read(reinterpret_cast<char*>(&samples), 8);
            Check(riffBytes + 8 == fs::file_size(wavePath) && dataBytes == 192000 && samples == 48000, "RF64 ds64 header sizes are wrong");
        }
        run(L"-xerror -i " + QuoteArg(wavePath.wstring()) + L" -f null NUL", L"rf64-decode");
        std::cout << "PASS: RF64 header and FFmpeg decode; leading silence and long timeline\n";

        {
            const auto interruptedPath = root / L"interrupted-fragments.mp4";
            ChildProcess interrupted;
            Check(interrupted.Start(ffmpeg, L"-y -hide_banner -loglevel error -re -f lavfi -i color=c=red:s=160x90:r=15 -an -c:v libx264 -preset ultrafast -g 15 -pix_fmt yuv420p -movflags +frag_keyframe+empty_moov+default_base_moof -flush_packets 1 " + QuoteArg(interruptedPath.wstring()),
                (root / L"interrupted.log").wstring(), true), "Synthetic fragment writer did not start");
            Sleep(2500);
            Check(interrupted.Running(), "Synthetic fragment writer exited early");
            interrupted.SendQuitAndWait(0); interrupted.Close();
            run(L"-xerror -i " + QuoteArg(interruptedPath.wstring()) + L" -map 0:v:0 -abort_on empty_output -f null NUL", L"interrupted-decode");
            std::cout << "PASS: flushed fragmented MP4 remains decodable after interrupted synthetic capture\n";
        }

        const auto session = root / L"silent then tone's session";
        RecordingManager mixed;
        PipelineTestAccess::Setup(mixed, session, root / L"mixed.mp4", ffmpeg, true);
        video(session / L"silent.mp4"); video(session / L"tone.mp4");
        run(L"-f lavfi -i sine=frequency=880:sample_rate=48000 -t 2 -c:a pcm_s16le " + QuoteArg((session / L"tone.wav").wstring()), L"tone");
        PipelineTestAccess::Add(mixed, session / L"silent.mp4");
        PipelineTestAccess::Add(mixed, session / L"tone.mp4", session / L"tone.wav");
        Check(mixed.LastOutput().empty(), "LastOutput exposed an unfinished file");
        const auto result = mixed.Stop();
        if (!result.ok) std::wcerr << result.message << L'\n';
        Check(result.ok && fs::exists(mixed.LastOutput()), "Mixed silent and audible segment finalization failed");
        Check(!fs::exists(session), "Successful recording did not clean session files");
        run(L"-i " + QuoteArg(mixed.LastOutput()) + L" -vn -ar 48000 -ac 1 -c:a pcm_s16le -f s16le " + QuoteArg((root / L"mixed.pcm").wstring()), L"mixed-decode");
        {
            std::ifstream pcm(root / L"mixed.pcm", std::ios::binary);
            std::vector<int16_t> samples(static_cast<size_t>(fs::file_size(root / L"mixed.pcm") / 2));
            pcm.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(samples.size() * 2));
            Check(samples.size() > 170000, "Concatenation lost part of audio duration");
            double first = 0, second = 0;
            for (size_t i = 24000; i < 72000; ++i) first += static_cast<double>(samples[i]) * samples[i];
            for (size_t i = 130000; i < 170000; ++i) second += static_cast<double>(samples[i]) * samples[i];
            Check(first < 1000 && second > 1e9, "Silent first segment caused later audio to disappear or move");
        }
        std::cout << "PASS: silent/audible paused segments retain audio and duration, apostrophe paths\n";

        const auto silentSession = root / L"no-audio";
        RecordingManager silent;
        PipelineTestAccess::Setup(silent, silentSession, root / L"silent.mp4", ffmpeg, false);
        video(silentSession / L"video.mp4"); PipelineTestAccess::Add(silent, silentSession / L"video.mp4");
        Check(silent.Stop().ok, "Video-only finalization failed");
        run(L"-xerror -i " + QuoteArg(silent.LastOutput()) + L" -map 0:v:0 -f null NUL", L"silent-decode");
        std::cout << "PASS: video-only segments are validated and remuxed\n";

        const auto corruptSession = root / L"corrupt";
        RecordingManager corrupt;
        PipelineTestAccess::Setup(corrupt, corruptSession, root / L"must-not-exist.mp4", ffmpeg, false);
        { std::ofstream file(corruptSession / L"broken.mp4", std::ios::binary); file << "invalid nonzero MP4"; }
        PipelineTestAccess::Add(corrupt, corruptSession / L"broken.mp4");
        const auto failure = corrupt.Stop();
        Check(!failure.ok && failure.message.find(corruptSession.wstring()) != std::wstring::npos, "Corrupt save incorrectly succeeded or omitted recovery path");
        Check(fs::exists(corruptSession / L"broken.mp4") && corrupt.LastOutput().empty() && !fs::exists(root / L"must-not-exist.mp4"), "Corrupt source was lost or published");
        std::cout << "PASS: corrupt nonzero MP4 is retained, never reported saved\n";

        const auto missingSession = root / L"missing-segment";
        RecordingManager missing;
        PipelineTestAccess::Setup(missing, missingSession, root / L"must-not-truncate.mp4", ffmpeg, false);
        video(missingSession / L"first.mp4");
        PipelineTestAccess::Add(missing, missingSession / L"first.mp4");
        PipelineTestAccess::Add(missing, missingSession / L"missing.mp4");
        Check(!missing.Stop().ok && fs::exists(missingSession / L"first.mp4") && missing.LastOutput().empty(), "Missing paused segment was silently skipped");
        std::cout << "PASS: missing paused segment does not create a falsely complete recording\n";

        const auto badAudioSession = root / L"bad-audio";
        RecordingManager badAudio;
        PipelineTestAccess::Setup(badAudio, badAudioSession, root / L"must-not-lose-audio.mp4", ffmpeg, true);
        video(badAudioSession / L"video.mp4");
        { std::ofstream file(badAudioSession / L"broken.wav"); file << "invalid audio"; }
        PipelineTestAccess::Add(badAudio, badAudioSession / L"video.mp4", badAudioSession / L"broken.wav");
        Check(!badAudio.Stop().ok && fs::exists(badAudioSession / L"video.mp4") && badAudio.LastOutput().empty(), "Audio corruption was silently ignored");
        std::cout << "PASS: corrupt captured audio produces a visible failure and preserves video\n";

        const auto stoppedSession = root / L"stop-failed";
        RecordingManager stopped;
        PipelineTestAccess::Setup(stopped, stoppedSession, root / L"must-not-publish.mp4", ffmpeg, false);
        video(stoppedSession / L"video.mp4"); PipelineTestAccess::Add(stopped, stoppedSession / L"video.mp4");
        PipelineTestAccess::Failure(stopped);
        Check(!stopped.Stop().ok && fs::exists(stoppedSession / L"video.mp4") && stopped.LastOutput().empty(), "Capture stop error was ignored");
        std::cout << "PASS: capture stop failures preserve recovery and prevent false success\n";

        const auto blockedSession = root / L"publish-failed";
        const auto blockedOutput = root / L"existing.mp4";
        { std::ofstream file(blockedOutput); file << "existing recording must survive"; }
        RecordingManager blocked;
        PipelineTestAccess::Setup(blocked, blockedSession, blockedOutput, ffmpeg, false);
        video(blockedSession / L"video.mp4"); PipelineTestAccess::Add(blocked, blockedSession / L"video.mp4");
        Check(!blocked.Stop().ok && fs::exists(blockedSession / L"finished.mp4") && blocked.LastOutput().empty(), "Output collision overwrote a file or discarded recovery");
        Check(fs::file_size(blockedOutput) == 31, "Existing recording was overwritten");
        std::cout << "PASS: publish collision preserves existing recording and recoverable output\n";
        std::cout << "All synthetic pipeline tests passed. No screen or microphone capture was performed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
