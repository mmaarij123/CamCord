#include "CaptureEngine.h"
#include <sstream>

static int BitrateKbps(const RecordingSettings& s) {
    if (s.height == 480) return s.fps >= 60 ? 5000 : (s.fps <= 15 ? 2500 : 3500);
    if (s.height == 720) return s.fps >= 60 ? 8000 : (s.fps <= 15 ? 4000 : 5500);
    if (s.fps >= 120) return 24000;
    if (s.fps >= 60) return 14000;
    if (s.fps <= 15) return 6000;
    return 9000;
}

OperationResult CaptureEngine::Start(const std::wstring& ffmpeg, const std::wstring& output, const std::wstring& log,
    const RecordingSettings& s, const EncoderChoice& encoder) {
    if (process_.Running()) return OperationResult::Failure(L"A recording is already running.");
    const int bitrate = BitrateKbps(s), maxrate = bitrate * 13 / 10, buffer = bitrate * 2;
    const auto size = std::to_wstring(s.width) + L":" + std::to_wstring(s.height);
    const auto input = L"ddagrab=output_idx=0:framerate=" + std::to_wstring(s.fps) + L":draw_mouse=0";
    const auto filter = L"[0:v]hwdownload,format=bgra,scale=" + size + L":force_original_aspect_ratio=decrease:flags=lanczos,"
                        L"pad=" + size + L":(ow-iw)/2:(oh-ih)/2:color=black,format=nv12[v]";
    std::wstring args = L"-y -hide_banner -loglevel warning -f lavfi -i " + QuoteArg(input)
        + L" -filter_complex " + QuoteArg(filter) + L" -map " + QuoteArg(L"[v]")
        + L" -an -c:v " + encoder.name;
    if (encoder.name == L"h264_nvenc") args += L" -preset p4 -tune hq -rc vbr";
    else if (encoder.name == L"h264_amf") args += L" -quality balanced -rc vbr_peak";
    else if (encoder.name == L"h264_qsv") args += L" -preset medium";
    else args += L" -preset veryfast -tune zerolatency";
    args += L" -b:v " + std::to_wstring(bitrate) + L"k -maxrate " + std::to_wstring(maxrate) + L"k -bufsize " + std::to_wstring(buffer)
        + L"k -g " + std::to_wstring(s.fps * 2) + L" -pix_fmt yuv420p -r " + std::to_wstring(s.fps)
        + L" -movflags +frag_keyframe+empty_moov+default_base_moof -flush_packets 1 " + QuoteArg(output);
    if (!process_.Start(ffmpeg, args, log, true)) return OperationResult::Failure(L"The capture process could not be started.");
    Sleep(700);
    if (!process_.Running()) return OperationResult::Failure(L"Desktop capture initialization failed. See the session log for details.");
    return OperationResult::Success();
}

OperationResult CaptureEngine::Stop() {
    if (!process_.Running()) { process_.Close(); return OperationResult::Failure(L"Recording stopped unexpectedly."); }
    const bool clean = process_.SendQuitAndWait(); process_.Close();
    return clean ? OperationResult::Success() : OperationResult::Failure(L"The capture process did not close cleanly.");
}
