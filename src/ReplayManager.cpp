#include "ReplayManager.h"
#include "HardwareEncoderDetector.h"
#include "RecordingQuality.h"
#include "VideoEncoding.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <cmath>
namespace fs = std::filesystem;
namespace {
struct Chunk { fs::path file; double start, end; };
std::vector<Chunk> Catalog(const fs::path& session) {
    std::ifstream file(session / L"video.log.csv"); std::string line; std::vector<Chunk> result;
    while (std::getline(file,line)) {
        try {
            const auto last=line.rfind(','), before=last==std::string::npos?last:line.rfind(',',last-1);
            if (before==std::string::npos || last==std::string::npos) continue;
            auto name=line.substr(0,before); if (name.size()>1 && name.front()=='"' && name.back()=='"') name=name.substr(1,name.size()-2);
            auto base=fs::u8path(name).filename();
            if (!std::regex_match(base.string(),std::regex("video_[0-9]{9}\\.mkv"))) continue;
            const auto start=std::stod(line.substr(before+1,last-before-1)), end=std::stod(line.substr(last+1));
            if (std::isfinite(start) && std::isfinite(end) && start>=0 && end>start) result.push_back({session/base,start,end});
        } catch (...) {}
    }
    return result;
}
std::wstring Number(double n) { std::wostringstream s; s.imbue(std::locale::classic()); s << std::fixed << std::setprecision(6) << n; return s.str(); }
std::wstring AudioName(const std::wstring& prefix,int index) { std::wostringstream s; s<<prefix<<L"_"<<std::setfill(L'0')<<std::setw(9)<<index<<L".wav";return s.str(); }
void List(const fs::path& path,const std::vector<fs::path>& files) {
    std::ofstream out(path,std::ios::binary);
    for (const auto& file:files) {
        auto name=file.generic_u8string(); size_t pos=0;while((pos=name.find('\'',pos))!=std::string::npos){name.replace(pos,1,"'\\''");pos+=4;}
        out<<"file '"<<name<<"'\n";
    }
    out.close();if(!out)throw std::runtime_error("Could not write replay list");
}
fs::path Pin(const fs::path& file,const fs::path& job) {
    const auto dest=job/file.filename();std::error_code ec;fs::create_hard_link(file,dest,ec);
    if(ec)fs::copy_file(file,dest,fs::copy_options::none);return dest;
}
}
ReplayManager::~ReplayManager() { Stop(!retain_); }
double ReplayManager::Elapsed() const { return std::chrono::duration<double>(std::chrono::steady_clock::now()-began_).count(); }
OperationResult ReplayManager::Start(RecordingSettings& settings,const std::wstring& folder) {
    if(!stopping_)return OperationResult::Failure(L"Instant Replay is already on.");
    settings_=settings;settings_.replaySeconds=NormalizeReplaySeconds(settings.replaySeconds);output_.SetCaptureFolder(folder);
    auto result=output_.EnsureCaptureFolder();if(!result.ok)return result;
    const auto budget=static_cast<unsigned long long>((VideoPeakBitrateKbps(settings_)*1000.0/8+1536000)*(settings_.replaySeconds+16)+512ull*1024*1024);
    if(!output_.EnsureFreeSpace(budget).ok)return OperationResult::Failure(L"Free more space for the selected replay duration/bitrate, or choose a lower quality.");
    session_=output_.NewSessionFolder();if(session_.empty())return OperationResult::Failure(L"Could not create the replay buffer folder.");
    try {
        ffmpeg_=FindFfmpeg();if(ffmpeg_.empty())throw std::runtime_error("Missing FFmpeg");
        HardwareEncoderDetector detector;encoder_=detector.Detect(ffmpeg_,session_);
        if(encoder_.name.empty())throw std::runtime_error("No encoder");
        if(settings_.fps==120 && (!encoder_.hardware || !detector.Supports(ffmpeg_,session_,encoder_,settings_)))settings_.fps=60;
        if(settings_.systemAudio){result=system_.Start(true,(fs::path(session_)/L"system").wstring(),true);if(!result.ok)throw std::runtime_error("System audio failed");}
        if(settings_.microphone){result=mic_.Start(false,(fs::path(session_)/L"mic").wstring(),true);if(!result.ok)throw std::runtime_error("Microphone failed");}
        result=capture_.Start(ffmpeg_,(fs::path(session_)/L"video_%09d.mkv").wstring(),(fs::path(session_)/L"video.log").wstring(),settings_,encoder_,true);
        if(!result.ok && encoder_.hardware){encoder_={L"libx264",L"Software H.264",false};if(settings_.fps==120)settings_.fps=60;
            result=capture_.Start(ffmpeg_,(fs::path(session_)/L"video_%09d.mkv").wstring(),(fs::path(session_)/L"video.log").wstring(),settings_,encoder_,true);}
        if(!result.ok)throw std::runtime_error("Capture failed");
        began_=capture_.StartedAt();
        systemOffset_=std::chrono::duration<double>(began_-system_.StartedAt()).count();micOffset_=std::chrono::duration<double>(began_-mic_.StartedAt()).count();
        settings.fps=settings_.fps;stopping_=false;healthy_=true;buffered_=0;
        maintenance_=std::thread(&ReplayManager::Maintain,this);return OperationResult::Success(L"Instant Replay is buffering. Capture stays on until you turn it off or close CamCord.");
    } catch (...) {Stop(false);return OperationResult::Failure((result.ok?L"Instant Replay could not start.":result.message)+L"\nRecovery files: "+session_);}
}
void ReplayManager::Prune() {
    const auto before=Elapsed()-settings_.replaySeconds-20;
    std::lock_guard lock(files_);
    const auto catalog=Catalog(session_);
    for(const auto& chunk:catalog)if(chunk.end<before){std::error_code ec;fs::remove(chunk.file,ec);}
    const int oldest=catalog.empty()?-1:std::stoi(catalog.front().file.filename().string().substr(6,9));
    for(const auto& item:fs::directory_iterator(session_)) {
        const auto name=item.path().filename().string();std::smatch match;
        // Entries which fell out of FFmpeg's bounded catalog are also closed.
        // This covers slow snapshot copies without touching the active file.
        if(oldest>=0&&std::regex_match(name,match,std::regex("video_([0-9]{9})\\.mkv"))&&std::stoi(match[1].str())<oldest){std::error_code ec;fs::remove(item.path(),ec);continue;}
        if(std::regex_match(name,match,std::regex("(system|mic)_([0-9]{9})\\.wav")) &&
           std::stod(match[2].str())*2+2-(match[1].str()=="system"?systemOffset_:micOffset_)<before) {
            // Never delete the currently-open audio chunk; its .done marker is required.
            const auto marker=fs::path(item.path().wstring()+L".done");if(!fs::exists(marker))continue;
            std::error_code ec;fs::remove(item.path(),ec);if(!ec)fs::remove(marker,ec);
        }
    }
}
void ReplayManager::Maintain() {
    while(!stopping_) {
        try {
            if(!capture_.Running() || (settings_.systemAudio&&!system_.Healthy()) || (settings_.microphone&&!mic_.Healthy()) || !output_.EnsureFreeSpace(256ull*1024*1024).ok){healthy_=false;break;}
            Prune();buffered_=std::max(0,std::min(settings_.replaySeconds,static_cast<int>(Elapsed())));
        } catch (...) {healthy_=false;break;}
        for(int i=0;i<5&&!stopping_;i++)Sleep(100);
    }
}
OperationResult ReplayManager::Stop(bool discard) {
    retain_=!discard;
    stopping_=true;if(maintenance_.joinable())maintenance_.join();healthy_=false;buffered_=0;
    capture_.Stop(10000);system_.Stop();mic_.Stop();
    if(discard&&!session_.empty()){std::error_code ec;fs::remove_all(session_,ec);session_.clear();}
    return OperationResult::Success(discard?L"Instant Replay is off. Unsaved buffer cleared.":L"Instant Replay stopped. Recovery files retained: "+session_);
}
OperationResult ReplayManager::SaveClip(double end) {
    std::wstring job;
    try {
        if(stopping_ || !healthy_ || !std::isfinite(end) || end<=0) return OperationResult::Failure(L"Turn Instant Replay on and let it buffer before saving a clip.");
        const auto start=std::max(0.0,end-settings_.replaySeconds);
        const auto space=static_cast<unsigned long long>((VideoPeakBitrateKbps(settings_)*1000.0/8+1536000)*(end-start)*2+512ull*1024*1024);
        if(!output_.EnsureFreeSpace(space).ok)return OperationResult::Failure(L"Free more recording-drive space to save this clip, or use a lower bitrate/duration.");
        const auto deadline=GetTickCount64()+15000;std::vector<Chunk> chunks;
        auto audioReady=[&](const std::wstring& prefix,double offset){return fs::exists(fs::path(session_)/(AudioName(prefix,std::max(0,static_cast<int>(std::ceil((end+offset)/2))-1))+L".done"));};
        do {
            chunks=Catalog(session_);
            if(!chunks.empty() && chunks.back().end>=end && (!settings_.systemAudio||audioReady(L"system",systemOffset_)) && (!settings_.microphone||audioReady(L"mic",micOffset_)))break;
            if(!healthy_ || GetTickCount64()>deadline)return OperationResult::Failure(L"The replay buffer could not finish the current chunk. Check the source, encoder and audio devices.");
            Sleep(50);
        } while(true);
        job=output_.NewSessionFolder();if(job.empty())throw std::runtime_error("Snapshot folder failed");
        std::vector<fs::path> videos;double first=-1;std::wstring audioArgs;int audioCount=0;
        {
            std::lock_guard lock(files_);
            double previous=-1;
            for(const auto& chunk:chunks)if(chunk.end>start&&chunk.start<end){
                if(previous>=0&&chunk.start-previous>0.1)throw std::runtime_error("Replay gap");previous=chunk.end;
                if(first<0)first=chunk.start;videos.push_back(Pin(chunk.file,job));}
            if(videos.empty()||first>start+0.1)throw std::runtime_error("Replay data unavailable");
            List(fs::path(job)/L"video.txt",videos);
            auto audio=[&](const std::wstring& prefix,double offset){
                const int begin=std::max(0,static_cast<int>(std::floor((start+offset)/2))), finish=static_cast<int>(std::ceil((end+offset)/2));
                std::vector<fs::path> files;for(int index=begin;index<finish;index++)files.push_back(Pin(fs::path(session_)/AudioName(prefix,index),job));
                const auto list=fs::path(job)/(prefix+L".txt");List(list,files);audioArgs+=L" -ss "+Number(start+offset-begin*2)+L" -f concat -safe 0 -i "+QuoteArg(list.wstring());audioCount++;
            };
            if(settings_.systemAudio)audio(L"system",systemOffset_);if(settings_.microphone)audio(L"mic",micOffset_);
        }
        const auto staging=(fs::path(job)/L"finished.mp4").wstring(),log=(fs::path(job)/L"clip.log").wstring();
        const auto mixed=(fs::path(job)/L"audio.wav").wstring();
        if(audioCount) {
            // Finish a finite PCM mix before video export. Replay chunks already
            // contain clock-aligned silence; an infinite apad here can stall the
            // multi-input scheduler at a fractional concat seek on FFmpeg 9.
            auto mix=L"-y -hide_banner -loglevel error -xerror"+audioArgs;
            if(audioCount==1)mix+=L" -map 0:a:0 -af "+QuoteArg(L"aresample=48000:async=1:first_pts=0");
            else mix+=L" -filter_complex "+QuoteArg(L"[0:a][1:a]amix=inputs=2:duration=longest:dropout_transition=2,aresample=48000:async=1:first_pts=0[a]")+L" -map "+QuoteArg(L"[a]");
            mix+=L" -t "+Number(end-start)+L" -c:a pcm_s16le -ar 48000 -ac 2 -rf64 always "+QuoteArg(mixed);
            if(ChildProcess::Run(ffmpeg_,mix,(fs::path(job)/L"audio-mix.log").wstring(),exportTimeout_)!=0)throw std::runtime_error("Clip audio mixing failed");
        }
        auto args=L"-y -hide_banner -loglevel error -xerror -ss "+Number(std::max(0.0,start-first))+L" -f concat -safe 0 -i "+QuoteArg((fs::path(job)/L"video.txt").wstring());
        if(audioCount)args+=L" -i "+QuoteArg(mixed);
        args+=L" -t "+Number(end-start)+L" -map 0:v:0";
        if(audioCount)args+=L" -map 1:a:0";
        // Re-encode to trim accurately to the hotkey time rather than a 2-second keyframe boundary.
        args+=VideoEncodingArguments(settings_,{L"libx264",L"Software H.264",false}).substr(4);
        if(audioCount)args+=L" -c:a aac -b:a 192k -ar 48000 -ac 2";else args+=L" -an";
        args+=L" -movflags +faststart "+QuoteArg(staging);
        // Healthy 5-minute exports can take longer than the process helper's
        // default 30 seconds. The asynchronous worker keeps the UI responsive.
        if(ChildProcess::Run(ffmpeg_,args,log,exportTimeout_)!=0)throw std::runtime_error("Clip encoding failed");
        const auto clip=(fs::path(output_.CaptureFolder())/(L"CamCord_Clip_"+fs::path(job).filename().wstring()+L".mp4")).wstring();
        if(!MoveFileExW(staging.c_str(),clip.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Clip publication failed");
        lastOutput_=clip;std::error_code ec;fs::remove_all(job,ec);return OperationResult::Success(L"Clip saved: "+fs::path(clip).filename().wstring());
    }catch(...){return OperationResult::Failure(L"Clip could not be saved. Replay continues; no saved files were overwritten.\nRecovery files: "+job);}
}
