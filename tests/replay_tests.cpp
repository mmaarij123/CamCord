// Synthetic media only: no user's desktop or microphone is captured.
#include "../src/ReplayManager.h"
#include "../src/AudioCaptureEngine.cpp"
#include "../src/SettingsManager.h"
#include "../src/HostMessage.h"
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <set>
#include <future>
namespace fs=std::filesystem;
static void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct ReplayTestAccess {
    static void Setup(ReplayManager& r,const fs::path& session,const fs::path& root,const std::wstring& ffmpeg) {
        r.session_=session.wstring();r.ffmpeg_=ffmpeg;r.output_.SetCaptureFolder(root.wstring());
        r.settings_.width=160;r.settings_.height=480;r.settings_.fps=15;r.settings_.bitrateMbps=2;r.settings_.systemAudio=true;r.settings_.microphone=true;
        r.stopping_=false;r.healthy_=true;r.began_=std::chrono::steady_clock::now()-std::chrono::milliseconds(307400);
    }
    static void Seconds(ReplayManager& r,int value){r.settings_.replaySeconds=value;}
    static void Prune(ReplayManager& r){r.Prune();}
};
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;const std::wstring ffmpeg=argv[1];const fs::path root=fs::absolute(argv[2])/L"O'Brien_اردو_🎬",session=root/L".camcord-sessions"/L"buffer";
    fs::create_directories(session);
    auto run=[&](const std::wstring& args,const std::wstring& log){Check(ChildProcess::Run(ffmpeg,L"-y -hide_banner -loglevel error "+args,(root/log).wstring(),120000)==0,"Synthetic replay FFmpeg failed");};
    try {
        for(int seconds:{30,60,90,120,180,240,300})Check(NormalizeReplaySeconds(seconds)==seconds,"Replay preset missing");
        Check(NormalizeReplaySeconds(301)==60,"Invalid duration accepted");Check(NormalizeReplayHotkey(L"Alt+F4")==L"Ctrl+Shift+F8","Reserved shortcut accepted");
        Check(ReplayKey(L"Ctrl+Alt+F10")==VK_F10&&ReplayKey(L"Ctrl+Shift+F8")==VK_F8,"Shortcut mapping failed");
        SettingsManager prefs((root/L"settings.ini").wstring());RecordingSettings s;s.replaySeconds=300;s.replayHotkey=L"Ctrl+Alt+F10";
        Check(prefs.Save(s).ok,"Preferences save failed");const auto loaded=prefs.Load();Check(loaded.replaySeconds==300&&loaded.replayHotkey==s.replayHotkey,"Replay preferences lost");
        HostMessage message;Check(message.Parse(LR"({"type":"settings","settings":{"replaySeconds":999,"replayHotkey":"Alt+F4"}})"),"Message parse failed");
        Check(message.Settings(s).replaySeconds==60&&message.Settings(s).replayHotkey==L"Ctrl+Shift+F8","Host validation failed");
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=16000;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=32000;
        AudioFiles audio;Check(audio.Open((root/L"rolling").wstring(),&format,true),"Rolling PCM open failed");Check(audio.Silence(9ull*32000)&&audio.Close(),"PCM splitting failed");
        for(int i=0;i<5;i++){std::wostringstream name;name<<L"rolling_"<<std::setfill(L'0')<<std::setw(9)<<i<<L".wav";Check(fs::exists(root/(name.str()+L".done"))&&fs::file_size(root/name.str())>=32000,"Closed PCM chunk truncated");}
        std::cout<<"PASS: strict replay preferences/hotkeys and continuous PCM splitting\n";
        OutputManager folders;folders.SetCaptureFolder(root.wstring());std::vector<std::future<std::wstring>> jobs;
        for(int i=0;i<40;i++)jobs.push_back(std::async(std::launch::async,[&folders]{return folders.NewSessionFolder();}));
        std::set<std::wstring> unique;for(auto& job:jobs){const auto folder=job.get();Check(!folder.empty(),"Concurrent session creation failed");unique.insert(folder);}
        Check(unique.size()==40,"Manual recording and clip sessions collided");
        run(L"-f lavfi -i color=c=blue:s=160x90:r=15 -vf "+QuoteArg(L"drawbox=color=red:t=fill:enable='gte(t,308)'")+L" -t 310 -an -c:v libx264 -preset ultrafast -g 30 -bf 0 -f segment -segment_time 2 -segment_format matroska -reset_timestamps 1 -segment_list_type csv -segment_list_size 162 -segment_list "+QuoteArg((session/L"video.log.csv").wstring())+L" "+QuoteArg((session/L"video_%09d.mkv").wstring()),L"generate.log");
        const auto tone=root/L"tone.wav";run(L"-f lavfi -i sine=frequency=660:sample_rate=48000 -t 2 -c:a pcm_s16le -ac 2 -rf64 always "+QuoteArg(tone.wstring()),L"tone.log");
        for(const auto* prefix:{L"system",L"mic"})for(int i=0;i<155;i++){std::wostringstream name;name<<prefix<<L"_"<<std::setfill(L'0')<<std::setw(9)<<i<<L".wav";fs::create_hard_link(tone,session/name.str());std::ofstream marker(session/(name.str()+L".done"));}
        ReplayManager replay;ReplayTestAccess::Setup(replay,session,root,ffmpeg);std::vector<std::wstring> outputs;
        for(int seconds:{300,240,180,120,90,60,30}) {
            ReplayTestAccess::Seconds(replay,seconds);auto result=replay.SaveClip(307.4);if(!result.ok)std::wcerr<<result.message<<L'\n';Check(result.ok,"Native clip save failed");outputs.push_back(replay.LastOutput());
            const auto pcm=root/L"duration.pcm";run(L"-xerror -i "+QuoteArg(outputs.back())+L" -map 0:a:0 -ar 8000 -ac 1 -f s16le "+QuoteArg(pcm.wstring()),L"decode.log");Check(std::abs(static_cast<double>(fs::file_size(pcm))/16000-seconds)<0.15,"Clip duration mismatch");
        }
        Check(std::set<std::wstring>(outputs.begin(),outputs.end()).size()==7,"Clips overwrite one another");
        const auto pixel=root/L"last.rgb";run(L"-sseof -0.2 -i "+QuoteArg(outputs.back())+L" -frames:v 1 -vf scale=1:1 -pix_fmt rgb24 -f rawvideo "+QuoteArg(pixel.wstring()),L"pixel.log");std::ifstream rgb(pixel,std::ios::binary);unsigned char bytes[3]{};rgb.read(reinterpret_cast<char*>(bytes),3);Check(bytes[2]>150&&bytes[0]<60,"Clip contains post-hotkey frames");
        ReplayTestAccess::Prune(replay);int videos=0,waves=0;for(const auto& item:fs::directory_iterator(session)){if(item.path().extension()==L".mkv")videos++;if(item.path().extension()==L".wav")waves++;}Check(videos<30&&waves<60,"Buffer not bounded");
        {std::ofstream bad(session/L"video_000000150.mkv",std::ios::binary|std::ios::trunc);bad<<"corrupt";}
        Check(!replay.SaveClip(307.4).ok,"Corrupt clip published");Check(replay.Healthy(),"Failed save stopped replay");replay.Stop(false);
        std::cout<<"PASS: all 30s–5min native clips, mixed audio, hotkey trim, unique publication, retention and recovery\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
