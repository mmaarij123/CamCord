import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { mkdir, mkdtemp, readFile, writeFile, stat, rm, link } from 'node:fs/promises';
import { performance } from 'node:perf_hooks';
import { once } from 'node:events';
import { ManagedProcess, run } from './process.mjs';
import { writableFolder, normalizeSettings, DEFAULT_SETTINGS } from './preferences.mjs';
import { videoBitrateKbps, videoPeakMbps } from '../shared/recording-quality.js';
import { FrameCache } from './frames.mjs';

export const HELPER = fileURLToPath(new URL('./capture_helper.py', import.meta.url));
export function encodingArgs(settings, encoder) {
  const kbps=videoBitrateKbps(settings), result=[];
  if (encoder.name === 'h264_vaapi') result.push('-vaapi_device', encoder.device, '-vf','format=nv12,hwupload');
  result.push('-c:v',encoder.name);
  if (encoder.name==='h264_nvenc') result.push('-preset','p4','-tune','hq','-rc','vbr');
  else if(encoder.name==='h264_qsv') result.push('-preset','medium');
  else if(encoder.name==='libx264') result.push('-preset','veryfast','-tune','zerolatency');
  result.push('-b:v',`${kbps}k`,'-maxrate',`${Math.round(videoPeakMbps(settings)*1000)}k`,'-bufsize',`${kbps*2}k`,'-g',String(settings.fps*2));
  if(encoder.name!=='h264_vaapi') result.push('-pix_fmt','yuv420p');
  return result;
}
export async function detectEncoder(ffmpeg, settings) {
  const choices=[{name:'h264_nvenc',label:'NVIDIA NVENC',hardware:true},{name:'h264_qsv',label:'Intel Quick Sync',hardware:true},
    {name:'h264_vaapi',label:'VA-API (AMD / Intel)',device:'/dev/dri/renderD128',hardware:true}, {name:'libx264',label:'Software H.264',hardware:false}];
  for(const encoder of choices) {
    if(!encoder.hardware && settings.fps===120) continue;
    try { await run(ffmpeg,['-hide_banner','-loglevel','error','-f','lavfi','-i',`color=c=black:s=${width(settings)}x${settings.height}:r=${settings.fps}`,
      '-frames:v','8','-an',...encodingArgs(settings,encoder),'-f','null','-'],{timeout:15000});return encoder; } catch {}
  }
  if(settings.fps===120) {settings.fps=60;return detectEncoder(ffmpeg,settings);}
  throw new Error('No working H.264 encoder is available. Install Debian’s ffmpeg package.');
}
export function width(settings) {return settings.height===480?854:settings.height===720?1280:1920;}
export function concatLine(file) {return `file '${file.replaceAll("'", "'\\''")}'\n`;}
export class Recorder {
  constructor({ffmpeg='ffmpeg', python='python3', helper=HELPER, onFailure=()=>{}} = {}) {
    Object.assign(this,{ffmpeg,python,helper,onFailure}); this.state='idle';this.settings={...DEFAULT_SETTINGS};this.elapsedMs=0;this.segments=[];
  }
  elapsedSeconds() {return Math.floor((this.elapsedMs+(this.state==='recording'?performance.now()-this.segmentAt:0))/1000);}
  get latest() {return this.frames?.latest ?? null;}
  async sources(portal, excludePid) {
    if(portal) return ['display','window'].map(kind=>({kind,id:`portal:${kind}`,label:`Choose ${kind==='display'?'screen':'window'} in the system picker when recording`,primary:kind==='display'}));
    return JSON.parse(await run(this.python,[this.helper,'list','--exclude-pid',String(excludePid)]));
  }
  async start(settings, folder, source, portal) {
    if(this.state!=='idle') throw new Error('A recording is already active.');
    this.settings=normalizeSettings(DEFAULT_SETTINGS,settings);
    await writableFolder(folder,512n*1024n*1024n);
    await mkdir(path.join(folder,'.camcord-sessions'),{recursive:true,mode:0o700});
    this.session=await mkdtemp(path.join(folder,'.camcord-sessions','session-'));
    this.output=path.join(folder,`CamCord_${new Date().toISOString().replaceAll(':','-')}_${path.basename(this.session).slice(-6)}.mp4`);
    this.segments=[];this.elapsedMs=0;this.failed=null;
    try {
      this.encoder=await detectEncoder(this.ffmpeg,this.settings);
      this.settings.width=width(this.settings);
      this.source=source;this.portal=portal;
      this.capture=new ManagedProcess(this.python,[this.helper,'capture','--kind',source.kind,'--id',source.id,'--width',String(this.settings.width),
        '--height',String(this.settings.height),'--fps',String(this.settings.fps),...(portal?['--portal']:[])],path.join(this.session,'source.log'));
      this.sourceStopping=false;this.frameBytes=this.settings.width*this.settings.height*4;this.frames=new FrameCache(this.frameBytes);
      this.capture.child.stdout.on('data', chunk=>{
        this.frames.accept(chunk);
      });
      this.capture.done.then(()=>{
        if(!this.sourceStopping && ['recording','paused'].includes(this.state)) this.onFailure('The selected source closed, was minimized, disconnected or its capture permission ended.');
      });
      const deadline=performance.now()+(portal?180000:10000);
      while(!this.latest && !this.capture.exited && performance.now()<deadline) await new Promise(resolve=>setTimeout(resolve,20));
      if(!this.latest) {
        const details=(await readFile(path.join(this.session,'source.log'),'utf8').catch(()=>'')).trim().slice(-1000);
        throw new Error((portal?'Capture was cancelled or the desktop portal did not provide frames. Choose a source and try again.':'No frames arrived. Restore the selected window or choose another source.')+(details?`\n${details}`:''));
      }
      try {await this.startSegment();} catch(error) {
        if(!error.encoderFailure || !this.encoder.hardware) throw error;
        await this.abortEncoder();this.segments.pop();this.failed=null;
        this.encoder={name:'libx264',label:'Software H.264',hardware:false};
        if(this.settings.fps===120)this.settings.fps=60;
        await this.startSegment();
      }
    } catch(error) {await this.abortSource();await this.abortEncoder();this.state='idle';throw this.recoveryError(error);}
  }
  async audioSources() {
    const result=[];
    if(this.settings.systemAudio) {
      const sink=(await run('pactl',['get-default-sink'])).trim();
      const sources=JSON.parse(await run('pactl',['--format=json','list','sources']));
      const monitor=sources.find(value=>value.name===`${sink}.monitor` || value.monitor_of_sink_name===sink);
      if(!monitor) throw new Error('No system playback monitor is available. Check PipeWire/PulseAudio, or turn System audio off.');
      result.push(monitor.name);
    }
    if(this.settings.microphone) {
      const mic=(await run('pactl',['get-default-source'])).trim();
      if(!mic || mic.endsWith('.monitor')) throw new Error('No default microphone is available. Select a microphone in your desktop sound settings.');
      result.push(mic);
    }
    return result;
  }
  async startSegment() {
    const file=path.join(this.session,`segment_${this.segments.length+1}.mkv`);
    const audios=await this.audioSources();
    this.audioStopping=false;this.audioProcesses=[];
    const audioFiles=[],epoch=(Date.now()/1000).toFixed(3);
    for(const [index,source] of audios.entries()) {
      const wave=path.join(this.session,`segment_${this.segments.length+1}_audio_${index}.wav`);
      const process=new ManagedProcess(this.ffmpeg,['-y','-hide_banner','-loglevel','warning','-copyts','-probesize','32','-analyzeduration','0',
        '-use_wallclock_as_timestamps','1','-f','pulse','-sample_rate','48000','-channels','2','-i',source,
        '-af',`asetpts=PTS-${epoch}/TB,aresample=48000:async=1:first_pts=0`,'-c:a','pcm_s16le','-ar','48000','-ac','2','-rf64','always',wave],`${wave}.log`);
      this.audioProcesses.push(process);audioFiles.push(wave);
      process.done.then(()=>{if(!this.audioStopping && this.state==='recording'){this.failed=new Error('An audio device stopped unexpectedly. Recovery files were retained.');this.onFailure(this.failed.message);}});
    }
    const args=['-y','-hide_banner','-loglevel','warning','-probesize','32','-analyzeduration','0','-use_wallclock_as_timestamps','1',
      '-f','rawvideo','-pixel_format','bgra','-video_size',`${this.settings.width}x${this.settings.height}`,'-framerate',String(this.settings.fps),'-i','pipe:0'];
    args.push('-map','0:v:0','-an',...encodingArgs(this.settings,this.encoder),'-r',String(this.settings.fps),'-cluster_time_limit','1000','-flush_packets','1',file);
    this.video=new ManagedProcess(this.ffmpeg,args,`${file}.log`);
    this.pumping=true;this.framesWritten=0;this.segmentAt=performance.now();this.state='recording';this.segments.push({video:file,audios:audioFiles});
    this.pumpTask=this.pump().catch(error=>{this.failed=error;this.pumping=false;this.onFailure(error.message);});
    await new Promise(resolve=>setTimeout(resolve,700));
    const deadline=performance.now()+10000;
    while(this.framesWritten<4 && !this.video.exited && !this.failed && performance.now()<deadline) await new Promise(resolve=>setTimeout(resolve,20));
    if(this.video.exited || this.failed || this.framesWritten<4) {
      const error=new Error('The video encoder or audio device stopped during startup. See the recovery log.');error.encoderFailure=true;throw error;
    }
  }
  async pump() {
    const interval=1000/this.settings.fps;let next=performance.now();
    while(this.pumping && !this.capture.exited) {
      const frame=this.latest;
      if(frame) {const cache=this.frames;cache.retain(frame);await this.video.write(frame);cache.release(frame);this.framesWritten++;}
      next+=interval;
      if(performance.now()-next>5000) throw new Error('The encoder cannot keep up. Lower the frame rate or bitrate before recording again.');
      await new Promise(resolve=>setTimeout(resolve,Math.max(0,next-performance.now())));
    }
  }
  async stopSegment() {
    if(this.state!=='recording') return;
    this.elapsedMs+=performance.now()-this.segmentAt;
    this.pumping=false;
    // Break only stalled input writes; otherwise finish the current frame.
    let timedOut=false;
    await Promise.race([this.pumpTask,new Promise(resolve=>{this.stopTimer=setTimeout(()=>{timedOut=true;this.video.child.kill('SIGINT');resolve();},5000);})]);
    clearTimeout(this.stopTimer);
    if(timedOut) {await this.pumpTask;this.failed ||= new Error('The encoder stopped accepting frames. Recovery files were retained.');}
    const video=this.video;this.video=null;
    try {await video.stop({timeout:this.failed?3000:0});} catch(error) {this.failed ||= error;}
    this.audioStopping=true;
    for(const audio of this.audioProcesses) {try{await audio.stop({interrupt:true,timeout:10000});}catch(error){this.failed ||= error;}}
    this.audioProcesses=[];
    if(this.failed) throw this.recoveryError(this.failed);
  }
  async pause() {
    if(this.state!=='recording') throw new Error('No recording can be paused.');
    try {await this.stopSegment();} finally {this.state='paused';this.capture.child.stdin.write('pause\n');}
  }
  async resume() {
    if(this.state!=='paused' || this.failed || this.capture.exited) throw this.recoveryError(new Error('This source cannot resume. Stop and save this session first.'));
    this.frames.reset();this.capture.child.stdin.write('resume\n');
    const deadline=performance.now()+10000;
    while(!this.latest && !this.capture.exited && performance.now()<deadline) await new Promise(resolve=>setTimeout(resolve,20));
    if(!this.latest) throw new Error('No new frames arrived from the selected source.');
    try {await this.startSegment();} catch(error) {await this.abortEncoder();this.state='paused';throw this.recoveryError(error);}
  }
  async stop() {
    if(!['recording','paused'].includes(this.state)) throw new Error('No recording is active.');
    let error;
    try {await this.stopSegment();} catch(value) {error=value;}
    this.state='saving';await this.abortSource();
    try {
      if(error || this.failed) throw error || this.failed;
      await this.finalize();
      this.lastOutput=this.output;await rm(this.session,{recursive:true,force:true});this.session='';return this.lastOutput;
    } catch(value) {throw this.recoveryError(value);} finally {this.state='idle';}
  }
  async finalize() {
    if(!this.segments.length) throw new Error('No video segment was produced.');
    const ready=[];
    const audioEnabled=this.settings.systemAudio || this.settings.microphone;
    for(const [index,segment] of this.segments.entries()) {
      const file=typeof segment==='string'?segment:segment.video;
      if(!(await stat(file)).size) throw new Error('An empty recording segment was retained for recovery.');
      await run(this.ffmpeg,['-hide_banner','-loglevel','error','-xerror','-err_detect','explode','-i',file,'-map','0:v:0','-an','-f','null','-'],{timeout:24*60*60*1000});
      const muxed=path.join(this.session,`ready_${index}.mkv`);
      const audios=[];
      if(audioEnabled) for(const wave of (typeof segment==='string'?[file]:segment.audios)) {
        const size=(await stat(wave)).size;
        if(size<128) {
          const header=await readFile(wave);
          if(header.length>=36 && header.toString('ascii',0,4)==='RF64' && header.toString('ascii',12,16)==='ds64' && header.readBigUInt64LE(28)===0n)continue;
        }
        const metadata=JSON.parse(await run(this.ffmpeg.replace(/ffmpeg(\.exe)?$/,'ffprobe$1'),['-v','error','-show_entries','format=duration:stream=codec_type','-of','json',wave]));
        if(metadata.streams.some(stream=>stream.codec_type==='audio')) audios.push(wave);
      }
      const args=['-y','-hide_banner','-loglevel','error','-xerror','-i',file];
      for(const wave of audios) args.push('-i',wave);
      if(audioEnabled) {
        if(!audios.length)args.push('-f','lavfi','-i','anullsrc=r=48000:cl=stereo');
        if(audios.length<2)args.push('-map','0:v:0','-map','1:a:0','-af','aresample=48000:async=1:first_pts=0,apad');
        else args.push('-filter_complex','[1:a]aresample=48000:async=1:first_pts=0[a1];[2:a]aresample=48000:async=1:first_pts=0[a2];[a1][a2]amix=inputs=2:duration=longest:dropout_transition=2,apad[a]',
          '-map','0:v:0','-map','[a]');
        args.push('-c:v','copy','-c:a','pcm_s16le','-ar','48000','-ac','2','-shortest');
      } else args.push('-map','0:v:0','-c:v','copy','-an');
      args.push(muxed);
      await run(this.ffmpeg,args,{timeout:24*60*60*1000});
      ready.push(muxed);
    }
    const list=path.join(this.session,'concat.txt'),finished=path.join(this.session,'finished.mp4');
    await writeFile(list,ready.map(concatLine).join(''),{mode:0o600});
    await run(this.ffmpeg,['-y','-hide_banner','-loglevel','error','-xerror','-f','concat','-safe','0','-i',list,'-map','0','-c:v','copy',
      ...(audioEnabled?['-c:a','aac','-b:a','192k','-ar','48000','-ac','2','-af','aresample=48000:async=1:first_pts=0']:['-an']),'-movflags','+faststart',finished],{timeout:24*60*60*1000});
    if(!(await stat(finished)).size) throw new Error('The finished recording is empty.');
    // Same filesystem: atomic publication without ever overwriting another file.
    try {await link(finished,this.output);} catch(error) {
      if(process.platform!=='linux' || !['EPERM','ENOTSUP','EOPNOTSUPP'].includes(error.code))throw error;
      // Removable filesystems without hard links still get atomic publication
      // with no replacement. Never fall back to an overwriting rename/copy.
      await run(this.python,[this.helper,'publish','--source',finished,'--output',this.output]);
    }
  }
  recoveryError(error) {return new Error(`${error.message}${this.session?`\nRecovery files: ${this.session}`:''}`);}
  async abortEncoder() {this.pumping=false;this.audioStopping=true;if(this.video){this.video.child.kill('SIGINT');await this.video.stop({interrupt:true,timeout:3000}).catch(()=>{});this.video=null;}for(const audio of this.audioProcesses||[])await audio.stop({interrupt:true,timeout:3000}).catch(()=>{});this.audioProcesses=[];if(this.pumpTask)await this.pumpTask;}
  async abortSource() {this.sourceStopping=true;if(this.capture){this.capture.child.kill('SIGTERM');await this.capture.stop({interrupt:true,timeout:3000}).catch(()=>{});this.capture=null;}this.frames=null;}
}
