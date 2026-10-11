import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { mkdir, mkdtemp, readFile, writeFile, stat, rm, link } from 'node:fs/promises';
import { performance } from 'node:perf_hooks';
import { once } from 'node:events';
import { ManagedProcess, run } from './process.mjs';
import { writableFolder, normalizeSettings, DEFAULT_SETTINGS } from './preferences.mjs';
import { videoBitrateKbps, videoPeakMbps } from '../shared/recording-quality.js';

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
      this.latest=null;this.remainder=Buffer.alloc(0);this.sourceStopping=false;this.frameBytes=this.settings.width*this.settings.height*4;
      this.capture.child.stdout.on('data', chunk=>{
        // At most one partial frame plus the latest complete frame is retained.
        const bytes=this.remainder.length?Buffer.concat([this.remainder,chunk]):chunk;
        const complete=Math.floor(bytes.length/this.frameBytes);
        if(complete) this.latest=Buffer.from(bytes.subarray((complete-1)*this.frameBytes,complete*this.frameBytes));
        this.remainder=Buffer.from(bytes.subarray(complete*this.frameBytes));
      });
      this.capture.done.then(()=>{
        if(!this.sourceStopping && ['recording','paused'].includes(this.state)) this.onFailure('The selected source closed, was minimized, disconnected or its capture permission ended.');
      });
      const deadline=performance.now()+(portal?180000:10000);
      while(!this.latest && !this.capture.exited && performance.now()<deadline) await new Promise(resolve=>setTimeout(resolve,20));
      if(!this.latest) throw new Error(portal?'Capture was cancelled or the desktop portal did not provide frames. Choose a source and try again.':'No frames arrived. Restore the selected window or choose another source.');
      await this.startSegment();
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
    const args=['-y','-hide_banner','-loglevel','warning','-probesize','32','-analyzeduration','0','-use_wallclock_as_timestamps','1',
      '-f','rawvideo','-pixel_format','bgra','-video_size',`${this.settings.width}x${this.settings.height}`,'-framerate',String(this.settings.fps),'-i','pipe:0'];
    for(const source of audios) args.push('-thread_queue_size','512','-use_wallclock_as_timestamps','1','-f','pulse','-sample_rate','48000','-channels','2','-i',source);
    args.push('-map','0:v:0',...encodingArgs(this.settings,this.encoder),'-r',String(this.settings.fps));
    if(audios.length===2) args.push('-filter_complex','[1:a]aresample=48000:async=1:first_pts=0[a1];[2:a]aresample=48000:async=1:first_pts=0[a2];[a1][a2]amix=inputs=2:duration=longest:dropout_transition=2[a]',
      '-map','[a]','-c:a','pcm_s16le','-ar','48000','-ac','2');
    else if(audios.length===1) args.push('-map','1:a:0','-af','aresample=48000:async=1:first_pts=0','-c:a','pcm_s16le','-ar','48000','-ac','2');
    else args.push('-an');
    // Keep audio lossless until the single final AAC encode. Independently
    // primed AAC segments otherwise overlap DTS across pause/resume boundaries.
    args.push('-cluster_time_limit','1000','-flush_packets','1',file);
    this.video=new ManagedProcess(this.ffmpeg,args,`${file}.log`);this.hasAudio=audios.length>0;
    this.pumping=true;this.segmentAt=performance.now();this.state='recording';this.segments.push(file);
    this.pumpTask=this.pump().catch(error=>{this.failed=error;this.pumping=false;this.onFailure(error.message);});
    await new Promise(resolve=>setTimeout(resolve,700));
    if(this.video.exited || this.failed) throw new Error('The video encoder or audio device stopped during startup. See the recovery log.');
  }
  async pump() {
    const interval=1000/this.settings.fps;let next=performance.now();
    while(this.pumping && !this.capture.exited) {
      const frame=this.latest;
      if(frame) await this.video.write(frame);
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
    try {await video.stop({interrupt:this.hasAudio,timeout:this.failed?3000:0});} catch(error) {this.failed ||= error;}
    if(this.failed) throw this.recoveryError(this.failed);
  }
  async pause() {
    if(this.state!=='recording') throw new Error('No recording can be paused.');
    try {await this.stopSegment();} finally {this.state='paused';this.capture.child.stdin.write('pause\n');}
  }
  async resume() {
    if(this.state!=='paused' || this.failed || this.capture.exited) throw this.recoveryError(new Error('This source cannot resume. Stop and save this session first.'));
    this.latest=null;this.capture.child.stdin.write('resume\n');
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
    for(const [index,file] of this.segments.entries()) {
      if(!(await stat(file)).size) throw new Error('An empty recording segment was retained for recovery.');
      await run(this.ffmpeg,['-hide_banner','-loglevel','error','-xerror','-err_detect','explode','-i',file,'-map','0:v:0','-an','-f','null','-'],{timeout:24*60*60*1000});
      const muxed=path.join(this.session,`ready_${index}.mkv`);
      await run(this.ffmpeg,['-y','-hide_banner','-loglevel','error','-xerror','-i',file,'-map','0','-c:v','copy','-c:a','pcm_s16le','-ar','48000','-ac','2',muxed],{timeout:24*60*60*1000});
      ready.push(muxed);
    }
    const list=path.join(this.session,'concat.txt'),finished=path.join(this.session,'finished.mp4');
    await writeFile(list,ready.map(concatLine).join(''),{mode:0o600});
    await run(this.ffmpeg,['-y','-hide_banner','-loglevel','error','-xerror','-f','concat','-safe','0','-i',list,'-map','0','-c:v','copy',
      '-c:a','aac','-b:a','192k','-ar','48000','-ac','2','-af','aresample=48000:async=1:first_pts=0','-movflags','+faststart',finished],{timeout:24*60*60*1000});
    if(!(await stat(finished)).size) throw new Error('The finished recording is empty.');
    // Same filesystem: atomic publication without ever overwriting another file.
    await link(finished,this.output);
  }
  recoveryError(error) {return new Error(`${error.message}${this.session?`\nRecovery files: ${this.session}`:''}`);}
  async abortEncoder() {this.pumping=false;if(this.video){this.video.child.kill('SIGINT');await this.video.stop({interrupt:true,timeout:3000}).catch(()=>{});this.video=null;}if(this.pumpTask)await this.pumpTask;}
  async abortSource() {this.sourceStopping=true;if(this.capture){this.capture.child.kill('SIGTERM');await this.capture.stop({interrupt:true,timeout:3000}).catch(()=>{});this.capture=null;}this.latest=null;this.remainder=null;}
}
