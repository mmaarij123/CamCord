// Exercises the production frame bridge with generated moving frames only.
// Never selects or records a desktop, window, sound device or microphone.
#include "../src/CaptureEngine.h"
#include "../src/CaptureSources.h"
#include "../src/RecordingQuality.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <future>

namespace fs = std::filesystem;
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct MovingWindow {
    HWND window = nullptr;
    HANDLE timer = nullptr;
    std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
    static LRESULT CALLBACK Paint(HWND window, UINT message, WPARAM w, LPARAM l) {
        if (message != WM_PAINT) return DefWindowProcW(window, message, w, l);
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        const auto level = static_cast<BYTE>(GetWindowLongPtrW(window, GWLP_USERDATA));
        const auto brush = CreateSolidBrush(RGB(level, level, level));
        RECT rect{}; GetClientRect(window, &rect); FillRect(dc, &rect, brush); DeleteObject(brush);
        EndPaint(window, &paint); return 0;
    }
    void Create() {
        WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = Paint;
        type.lpszClassName = L"CamCordMovingCadenceFixture";
        Check(RegisterClassW(&type) != 0, "Cannot register moving test window");
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"CamCord generated motion test",
            WS_POPUP, 80, 80, 640, 360, nullptr, nullptr, type.hInstance, nullptr);
        timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        Check(window && timer, "Cannot create moving fixture/timer"); ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    void Step(int fps) {
        MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - origin).count() * fps / 1000000000ll;
        const auto level = static_cast<LONG_PTR>(ticks % 20 * 10 + 24);
        if (GetWindowLongPtrW(window, GWLP_USERDATA) != level) {
            SetWindowLongPtrW(window, GWLP_USERDATA, level); InvalidateRect(window, nullptr, FALSE); UpdateWindow(window);
        }
        LARGE_INTEGER due{}; due.QuadPart = -10000;
        Check(SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE, "Fixture timer failed");
        WaitForSingleObject(timer, 1000);
    }
    ~MovingWindow() { if (window) DestroyWindow(window); if (timer) CloseHandle(timer); }
};
struct CaptureEngineTestAccess {
    static OperationResult Start(CaptureEngine& engine, const std::wstring& ffmpeg, const fs::path& output,
        const RecordingSettings& settings, const EncoderChoice& encoder) {
        // Changing solid luminance identifies repeated/skipped frames after
        // lossy H.264 decode. Generate at 16x16, then scale without heavy geq work.
        const auto input = L"nullsrc=s=16x16:r=" + std::to_wstring(settings.fps)
            + L",geq=lum='mod(N,20)*10+24':cb=128:cr=128,scale=" + std::to_wstring(settings.width)
            + L":" + std::to_wstring(settings.height) + L":flags=neighbor,format=nv12,realtime";
        const auto args = L"-hide_banner -loglevel warning -f lavfi -i " + QuoteArg(input)
            + L" -an -c:v rawvideo -threads 1 -fps_mode passthrough -pix_fmt nv12 -f rawvideo pipe:1";
        return engine.StartStream(ffmpeg, output.wstring(), output.wstring() + L".log", settings, encoder, args);
    }
};

int wmain(int argc, wchar_t** argv) {
    if (argc < 4 || argc > 7) return 2;
    const auto root = fs::absolute(argv[2]);
    bool hardware = false, live = false, uhd = false;
    for (int i = 4; i < argc; ++i) {
        const std::wstring option = argv[i];
        if (option == L"--hardware") hardware = true;
        else if (option == L"--live-window") live = true;
        else if (option == L"--uhd") uhd = true;
        else return 2;
    }
    if ((live || uhd) && !hardware) return 2;
    try {
        Check(!fs::exists(root), "Supply a fresh performance-test folder"); fs::create_directories(root);
        MovingWindow fixture;
        if (live) fixture.Create();
        const std::vector<std::pair<int,int>> profiles = uhd ? std::vector<std::pair<int,int>>{{1440,60},{2160,60}}
            : hardware ? std::vector<std::pair<int,int>>{{1080,60},{1080,120}} : std::vector<std::pair<int,int>>{{720,60}};
        for (const auto& [height, fps] : profiles) {
            RecordingSettings settings; settings.height = height; settings.width = RecordingWidth(height);
            settings.fps = fps; settings.systemAudio = settings.microphone = false;
            const EncoderChoice encoder = hardware ? EncoderChoice{L"h264_nvenc",L"NVIDIA",true} : EncoderChoice{L"libx264",L"Software",false};
            const auto output = root / (L"pacing-" + std::to_wstring(height) + L"-" + std::to_wstring(fps) + L".mp4");
            CaptureEngine capture;
            OperationResult result;
            if (live) {
                settings.captureTarget = CaptureWindowTarget(fixture.window);
                auto start = std::async(std::launch::async, [&] {
                    return capture.Start(argv[1], output.wstring(), output.wstring() + L".log", settings, encoder);
                });
                while (start.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) fixture.Step(fps);
                result = start.get();
            } else result = CaptureEngineTestAccess::Start(capture, argv[1], output, settings, encoder);
            if (!result.ok) std::wcerr << result.message << L'\n';
            Check(result.ok, "Synthetic frame bridge did not start");
            const auto began = capture.StartedAt();
            if (live) {
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                while (std::chrono::steady_clock::now() < end) fixture.Step(fps);
            } else Sleep(4000);
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            Check(capture.Running() && capture.Stop(15000).ok, "Synthetic frame bridge did not finish cleanly");
            const auto metadata = output.wstring() + L".metadata";
            Check(ChildProcess::Run(argv[3], L"-v error -select_streams v:0 -show_entries stream=width,height,avg_frame_rate -of csv=p=0 "
                + QuoteArg(output.wstring()), metadata) == 0, "Could not inspect recording metadata");
            std::ifstream metadataFile(metadata); std::string profile; std::getline(metadataFile, profile);
            if (!profile.empty() && profile.back() == '\r') profile.pop_back();
            Check(profile == std::to_string(settings.width) + "," + std::to_string(height) + "," + std::to_string(fps) + "/1",
                "Encoded file has the wrong dimensions or FPS");
            const auto pixels = output.wstring() + L".gray";
            Check(ChildProcess::Run(argv[1], L"-y -hide_banner -loglevel error -xerror -i " + QuoteArg(output.wstring())
                + L" -vf scale=1:1:flags=area -pix_fmt gray -f rawvideo " + QuoteArg(pixels), output.wstring() + L".decode.log") == 0,
                "Synthetic recording did not decode");
            std::ifstream file(pixels, std::ios::binary);
            std::vector<unsigned char> frames((std::istreambuf_iterator<char>(file)), {});
            Check(frames.size() > static_cast<size_t>(seconds * fps * 0.90), "Recording duration lost frames");
            size_t repeated = 0;
            // Exclude initial encoder setup and final drain from cadence measurement.
            const size_t trim = static_cast<size_t>(fps / 2);
            for (size_t i = trim + 1; i + trim < frames.size(); ++i)
                if (std::abs(int(frames[i]) - int(frames[i-1])) < 5) ++repeated;
            const auto measured = frames.size() - trim * 2 - 1;
            std::cout << (live ? "WINDOW CADENCE: " : "SYNTHETIC CADENCE: ") << settings.width << 'x' << settings.height << '@' << fps
                << " frames=" << frames.size() << " elapsed=" << seconds << " repeated=" << repeated
                << '/' << measured << " (" << 100.0 * repeated / measured << "%)\n";
            // Shared CI hosts can suffer scheduling contention. The stricter
            // hardware run is opt-in on a real GPU; both reject sustained stalls.
            Check(repeated < measured * (hardware ? 0.15 : 0.35), "Moving recording repeats too many frames");
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
