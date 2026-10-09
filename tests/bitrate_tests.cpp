#include "../src/VideoEncoding.h"
#include "../src/Process.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <limits>

namespace fs = std::filesystem;
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 && argc != 4) return 2;
    const std::wstring testEncoder = argc == 4 ? argv[3] : L"libx264";
    if (testEncoder != L"libx264" && testEncoder != L"h264_nvenc" && testEncoder != L"h264_amf" && testEncoder != L"h264_qsv") return 2;
    const auto root = fs::absolute(argv[2]);
    if (fs::exists(root)) { std::cerr << "Supply a fresh test folder.\n"; return 2; }
    try {
        fs::create_directories(root);
        for (const int invalid : { -1, 1, 3, 101, 1000000, std::numeric_limits<int>::max() })
            Check(NormalizeBitrateMbps(invalid) == 0, "Invalid bitrate did not become Auto");
        for (const auto& invalid : { L"", L"garbage", L"16.5", L"100e2", L"101", L"999999999999999999999999", L"16trailing" })
            Check(BitrateFromPreference(invalid) == 0, "Corrupt preference did not become Auto");
        Check(BitrateFromPreference(L"100") == 100, "Valid bitrate preference was rejected");
        const int expected[3][4] = { {2500,3500,5000,5000}, {4000,5500,8000,8000}, {6000,9000,14000,24000} };
        const int heights[] = { 480, 720, 1080 };
        const int frames[] = { 15, 30, 60, 120 };
        RecordingSettings settings;
        for (int row = 0; row < 3; ++row) for (int col = 0; col < 4; ++col) {
            settings.height = heights[row]; settings.fps = frames[col]; settings.bitrateMbps = 0;
            Check(VideoBitrateKbps(settings) == expected[row][col], "Auto bitrate regressed");
        }
        std::cout << "PASS: original Auto targets and invalid bitrate normalization\n";
        settings.height = argc == 4 ? 1080 : 720;
        settings.width = argc == 4 ? 1920 : 1280;
        settings.fps = argc == 4 ? 60 : 30;
        for (const int rate : BITRATE_PRESETS_MBPS) {
            settings.bitrateMbps = rate;
            Check(VideoPeakBitrateKbps(settings) <= 100000, "Peak exceeds the 100 Mbps ceiling");
            Check(VideoPeakBitrateKbps(settings) >= VideoBitrateKbps(settings), "Peak is below the target");
            if (rate) Check(VideoBitrateKbps(settings) == rate * 1000, "Manual preset was ignored");
            for (const auto& name : { L"h264_nvenc", L"h264_amf", L"h264_qsv", L"libx264" }) {
                const auto flags = VideoEncodingArguments(settings, {name, L"Test", false});
                Check(flags.find(L" -b:v " + std::to_wstring(VideoBitrateKbps(settings)) + L"k ") != std::wstring::npos,
                    "An encoder did not receive the selected bitrate");
                Check(flags.find(L" -maxrate " + std::to_wstring(VideoPeakBitrateKbps(settings)) + L"k ") != std::wstring::npos,
                    "An encoder did not receive the capped peak bitrate");
            }
            const auto output = root / (L"bitrate-" + std::to_wstring(rate) + L".mp4");
            const auto log = root / (L"bitrate-" + std::to_wstring(rate) + L".log");
            // Synthetic source only: these tests never capture the user's screen/audio.
            const auto input = argc == 4 ? L"testsrc2=size=1920x1080:rate=60" : L"testsrc2=size=640x360:rate=30";
            const auto flags = L"-y -hide_banner -loglevel warning -f lavfi -i " + std::wstring(input) + L" -t 0.5"
                + VideoEncodingArguments(settings, {testEncoder, L"Test encoder", argc == 4})
                + L" -movflags +frag_keyframe+empty_moov+default_base_moof " + QuoteArg(output.wstring());
            Check(ChildProcess::Run(argv[1], flags, log.wstring(), 30000) == 0, "A preset failed real FFmpeg encoding");
            Check(fs::file_size(output) > 1000, "A preset produced an empty MP4");
            Check(ChildProcess::Run(argv[1], L"-hide_banner -loglevel error -xerror -i " + QuoteArg(output.wstring())
                + L" -map 0:v:0 -an -f null NUL", log.wstring() + L".decode", 30000) == 0,
                "A preset produced an undecodable MP4");
        }
        std::wcout << L"PASS: all 16 presets encode and decode with " << testEncoder
            << L"; all encoder flags match; peak capped at 100 Mbps\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
