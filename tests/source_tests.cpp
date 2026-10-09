#include "../src/CaptureSources.h"
#include "../src/RegionSelector.h"
#include "../src/CaptureEngine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <future>

namespace fs = std::filesystem;
static void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
        RECT rect{}; GetClientRect(window, &rect);
        const auto value = GetWindowLongPtrW(window, GWLP_USERDATA);
        const HBRUSH color = CreateSolidBrush(value == 1 ? RGB(220, 20, 20) : value == 2 ? RGB(10, 220, 20) : RGB(10, 20, 220));
        FillRect(dc, &rect, color); DeleteObject(color); EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
static void Pump(DWORD duration) {
    const auto end = GetTickCount64() + duration;
    do {
        MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(10);
    } while (GetTickCount64() < end);
}

int wmain(int argc, wchar_t** argv) {
    // Owned fake FFmpeg: produces only synthetic BGRA bytes and simulates an
    // encoder that never consumes stdin. No user screen/audio is accessed.
    if (argc > 3 && std::wstring(argv[1]) == L"-hide_banner") {
        std::vector<BYTE> frame(640 * 360 * 4, 128);
        for (;;) {
            DWORD written = 0;
            if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr)) return 0;
        }
    }
    if (argc > 3 && std::wstring(argv[1]) == L"-y") { Sleep(60000); return 0; }
    if (argc < 3 || argc > 5) return 2;
    const bool live = argc >= 4 && std::wstring(argv[3]) == L"--live-window";
    const bool hardware = argc == 5 && std::wstring(argv[4]) == L"--hardware";
    const EncoderChoice testEncoder = hardware ? EncoderChoice{L"h264_nvenc",L"NVIDIA",true} : EncoderChoice{L"libx264",L"Software",false};
    const auto root = fs::absolute(argv[2]);
    if (fs::exists(root)) return 2;
    HWND fixture = nullptr, cover = nullptr;
    try {
        fs::create_directories(root);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CaptureTarget display; display.handle = 42; display.sourceWidth = 1920; display.sourceHeight = 1080;
        CaptureTarget region;
        Check(CaptureRegionFromDrag(display, 900, 700, 100, 100, region) && region.x == 100 && region.y == 100 &&
            region.regionWidth == 800 && region.regionHeight == 600, "Reverse drag geometry is incorrect");
        Check(CaptureRegionFromDrag(display, -200, -100, 2100, 1200, region) && region.x == 0 && region.y == 0 &&
            region.regionWidth == 1920 && region.regionHeight == 1080, "Drag escaped the selected display");
        region.label = L"Keep existing selection";
        const auto previous = region;
        Check(!CaptureRegionFromDrag(display, 0, 0, 4, 4, region) && region.label == previous.label &&
            region.regionWidth == previous.regionWidth, "Invalid drag changed previous selection");
        Check(!ValidCaptureRegion(1920,1080,-1,0,640,360) && !ValidCaptureRegion(1920,1080,1900,0,640,360) &&
            !ValidCaptureRegion(1920,1080,0,0,std::numeric_limits<int>::max(),360), "Invalid/overflow crop accepted");
        region = display; region.kind = CaptureKind::Region; region.x = 100; region.y = 200; region.regionWidth = 640; region.regionHeight = 360;
        const auto crop = CaptureInputFilter(region, 60);
        Check(crop.find(L"hmonitor=42") != std::wstring::npos && crop.find(L"crop_right=1180") != std::wstring::npos &&
            crop.find(L"crop_bottom=520") != std::wstring::npos, "Selected area does not use monitor-local crop coordinates");
        Check(CaptureInputFilter(display, 30).find(L"hmonitor=42") != std::wstring::npos, "Display selection ignored explicit monitor handle");
        std::cout << "PASS: forward/reverse/outside drag bounds, minimum size, overflow guard, monitor-relative crop\n";

        WNDCLASSEXW type{sizeof(type)}; type.lpfnWndProc = FixtureProc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"CamCordSyntheticSourceTest";
        Check(RegisterClassExW(&type) != 0, "Could not register synthetic fixture");
        fixture = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"CamCord synthetic capture fixture",
            WS_POPUP, 80, 80, 640, 360, nullptr, nullptr, type.hInstance, nullptr);
        Check(fixture != nullptr, "Could not create synthetic fixture");
        auto window = CaptureWindowTarget(fixture);
        Check(CaptureSourceExists(window), "Window identity is not valid");
        auto wrong = window; ++wrong.processId;
        Check(!CaptureSourceExists(wrong), "Reused/mismatched window process was accepted");
        Check(CaptureInputFilter(window, 30).find(L"hwnd=" + std::to_wstring(window.handle)) != std::wstring::npos &&
            CaptureInputFilter(window, 30).find(L"hmonitor=") == std::wstring::npos, "Window capture substituted a desktop crop");
        Check(!ResolveCaptureTarget(window).ok, "Hidden window should require restoration");
        std::cout << "PASS: specific HWND, identity check, no title matching/desktop fallback, hidden-window rejection\n";

        {
            ShowWindow(fixture, SW_SHOWNOACTIVATE); UpdateWindow(fixture);
            RecordingSettings syntheticSettings; syntheticSettings.width = 640; syntheticSettings.height = 360;
            syntheticSettings.fps = 30; syntheticSettings.systemAudio = syntheticSettings.microphone = false;
            syntheticSettings.captureTarget = CaptureWindowTarget(fixture);
            std::vector<wchar_t> executable(32768);
            const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            Check(length && length < executable.size(), "Cannot locate synthetic bridge fixture");
            CaptureEngine stalled;
            Check(stalled.Start(std::wstring(executable.data(), length), (root / L"stalled.mp4").wstring(),
                (root / L"stalled.log").wstring(), syntheticSettings, {L"libx264",L"Synthetic",false}).ok,
                "Synthetic bridge did not start");
            const auto began = GetTickCount64();
            Check(!stalled.Stop(250).ok && !stalled.Running() && GetTickCount64() - began < 3000,
                "Capture stop deadline did not cover blocked raw-input writer");
            ShowWindow(fixture, SW_HIDE);
            std::cout << "PASS: full producer/bridge/stalled-encoder stop returns within deadline and reports failure\n";
        }

        // Independently validate the crop with synthetic media, never desktop pixels.
        const auto synthetic = root / L"crop.mp4";
        const auto filter = L"crop=640:360:100:200,scale=854:480:force_original_aspect_ratio=decrease,pad=854:480:(ow-iw)/2:(oh-ih)/2,format=yuv420p";
        Check(ChildProcess::Run(argv[1], L"-y -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=15 -t 0.3 -vf "
            + QuoteArg(filter) + L" -c:v libx264 -preset ultrafast " + QuoteArg(synthetic.wstring()), (root / L"crop.log").wstring()) == 0,
            "Region crop/scale did not encode");
        Check(ChildProcess::Run(argv[1], L"-hide_banner -loglevel error -xerror -i " + QuoteArg(synthetic.wstring()) + L" -f null NUL",
            (root / L"crop-decode.log").wstring()) == 0, "Region output is not decodable");
        std::cout << "PASS: synthetic selected-area crop/scale encodes and decodes\n";

        if (live) {
            ShowWindow(fixture, SW_SHOWNOACTIVATE); UpdateWindow(fixture); Pump(250);
            RecordingSettings settings; settings.width = hardware ? 1920 : 640; settings.height = hardware ? 1080 : 360; settings.fps = hardware ? 120 : 30;
            settings.bitrateMbps = hardware ? 100 : 0;
            settings.systemAudio = settings.microphone = false; settings.captureTarget = CaptureWindowTarget(fixture);
            CaptureEngine capture;
            const auto video = root / L"live-window.mp4";
            auto startTask = std::async(std::launch::async, [&] { return capture.Start(argv[1], video.wstring(),
                (root / L"live-window.log").wstring(), settings, testEncoder); });
            while (startTask.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) Pump(20);
            const auto started = startTask.get();
            Check(started.ok, "Real window capture could not start; inspect live-window.log");
            Pump(500);
            cover = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"CamCord synthetic cover",
                WS_POPUP, 80, 80, 640, 360, nullptr, nullptr, type.hInstance, nullptr);
            SetWindowLongPtrW(cover, GWLP_USERDATA, 1); ShowWindow(cover, SW_SHOWNOACTIVATE); UpdateWindow(cover);
            // Change the selected window while it is occluded: proves that the
            // capture updates the window surface, not merely the first frame.
            SetWindowLongPtrW(fixture, GWLP_USERDATA, 2);
            InvalidateRect(fixture, nullptr, FALSE); UpdateWindow(fixture);
            Pump(900);
            auto stopTask = std::async(std::launch::async, [&] { return capture.Stop(12000); });
            while (stopTask.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) Pump(20);
            Check(stopTask.get().ok, "Static-window capture did not drain cleanly within test deadline");
            const auto pixel = root / L"pixel.rgb";
            Check(ChildProcess::Run(argv[1], L"-y -hide_banner -loglevel error -sseof -0.2 -i " + QuoteArg(video.wstring())
                + L" -frames:v 1 -vf scale=1:1 -pix_fmt rgb24 -f rawvideo " + QuoteArg(pixel.wstring()), (root / L"pixel.log").wstring()) == 0,
                "Could not decode real window capture");
            std::ifstream input(pixel, std::ios::binary); unsigned char rgb[3]{}; input.read(reinterpret_cast<char*>(rgb), 3);
            Check(input.gcount() == 3 && rgb[1] > 100 && rgb[0] < 60 && rgb[2] < 60,
                "Occluded capture did not update to green; it recorded the cover/desktop or froze on the first blue frame");
            // Also start a fresh recording with the window already occluded.
            CaptureEngine second;
            const auto secondVideo = root / L"already-occluded.mp4";
            auto secondStart = std::async(std::launch::async, [&] { return second.Start(argv[1], secondVideo.wstring(),
                (root / L"already-occluded.log").wstring(), settings, testEncoder); });
            while (secondStart.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) Pump(20);
            Check(secondStart.get().ok, "Window capture could not start while already covered");
            Pump(400); DestroyWindow(fixture); fixture = nullptr;
            auto secondStop = std::async(std::launch::async, [&] { return second.Stop(12000); });
            while (secondStop.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) Pump(20);
            Check(secondStop.get().ok, "Closed-window capture did not finish safely");
            Check(ChildProcess::Run(argv[1], L"-hide_banner -loglevel error -xerror -i " + QuoteArg(secondVideo.wstring())
                + L" -f null NUL", (root / L"already-occluded.decode.log").wstring()) == 0, "Closed-window recording is not decodable");
            std::cout << "PASS: occluded window updates from blue to green behind a red cover; static/closed capture drains cleanly; already-occluded startup works\n";
        }
        if (fixture) { DestroyWindow(fixture); fixture = nullptr; }
        auto closed = window;
        Check(!CaptureSourceExists(closed) && !ResolveCaptureTarget(closed).ok, "Closed window fell back to another source");
        std::cout << "PASS: closed source is rejected; no screen or microphone content captured by synthetic tests\n";
        if (cover) DestroyWindow(cover);
        return 0;
    } catch (const std::exception& error) {
        if (cover) DestroyWindow(cover); if (fixture) DestroyWindow(fixture);
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
