import { Recorder, encodingArgs, concatLine } from './recorder.mjs';
import { run } from './process.mjs';
import { readFile, readdir, unlink, mkdtemp, writeFile, link, copyFile, rm, statfs } from 'node:fs/promises';
import path from 'node:path';
import { performance } from 'node:perf_hooks';
import { videoPeakMbps } from '../shared/recording-quality.js';
import { writableFolder } from './preferences.mjs';
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
export function parseCatalog(text,folder,prefix,extension) {
  const pattern=new RegExp(`^${prefix}_[0-9]{9}\\.${extension}$`);
  return text.split('\n').flatMap(line=>{
    const match=line.trim().match(/^(.*),([\d.]+),([\d.]+)$/);if(!match)return[];
    const name=path.basename(match[1].replace(/^"|"$/g,'').replaceAll('""','"')),start=Number(match[2]),end=Number(match[3]);
    return pattern.test(name)&&Number.isFinite(start)&&Number.isFinite(end)&&start>=0&&end>start?[{file:path.join(folder,name),start,end}]:[];
  });
}
export function selectChunks(chunks,start,end) {
  const chosen=chunks.filter(chunk=>chunk.end>start&&chunk.start<end);
  if(!chosen.length || chosen[0].start>start+0.1 || chosen.at(-1).end<end-0.05 || chosen.some((chunk,index)=>index&&chunk.start-chosen[index-1].end>0.1))throw new Error('Replay chunks are missing. Let the buffer refill.');
  return chosen;
}
export class Replay extends Recorder {
  constructor(options) {super(options);this.rolling=true;this.pinned=new Set();this.buffered=0;this.enabled=false;}
  elapsed() {return (performance.now()-this.segmentAt)/1000;}
  healthy() {return this.enabled && !this.failed && !this.capture?.exited && !this.video?.exited;}
  async start(settings,folder,source,portal) {
    const budget=BigInt(Math.ceil((videoPeakMbps(settings)*1000000/8+384000)*(settings.replaySeconds+16)+512*1024*1024));
    await writableFolder(folder,budget);
    await super.start(settings,folder,source,portal);this.enabled=true;this.folder=folder;
    this.maintenance=setInterval(()=>{if(!this.pruning)this.pruning=this.prune().catch(error=>{this.failed=error;}).finally(()=>{this.pruning=null;});},500);
  }
  async catalog(prefix='video',extension='mkv') {return parseCatalog(await readFile(path.join(this.session,`${prefix}.csv`),'utf8').catch(()=>''),this.session,prefix,extension);}
  async prune() {
    const before=this.elapsed()-this.settings.replaySeconds-20;
    const catalogs=[await this.catalog()];
    for(let index=0;index<this.audioProcesses.length;index++)catalogs.push((await this.catalog(`audio_${index}`,'wav')).map(chunk=>({...chunk,end:chunk.end-this.audioOffset})));
    for(const chunks of catalogs)for(const chunk of chunks)if(chunk.end<before&&!this.pinned.has(chunk.file))await unlink(chunk.file).catch(error=>{if(error.code!=='ENOENT')throw error;});
    const space=await statfs(this.session,{bigint:true});if(space.bavail*space.bsize<256n*1024n*1024n)throw new Error('Replay stopped: the recording drive is nearly full.');
    this.buffered=Math.max(0,Math.min(this.settings.replaySeconds,Math.floor(this.elapsed())));
  }
  async stop(discard=true) {
    clearInterval(this.maintenance);if(this.pruning)await this.pruning;this.enabled=false;this.buffered=0;
    try {await this.stopSegment();}catch{}await this.abortSource();await this.abortEncoder();this.state='idle';
    if(discard&&this.session){await rm(this.session,{recursive:true,force:true});this.session='';}
  }
  async saveClip(end=this.elapsed()) {
    if(!this.healthy()||!Number.isFinite(end)||end<=0)throw new Error('Turn Instant Replay on and let it buffer before saving a clip.');
    const start=Math.max(0,end-this.settings.replaySeconds),deadline=Date.now()+15000;let video,audio;
    const space=await statfs(this.folder,{bigint:true});
    const required=BigInt(Math.ceil((videoPeakMbps(this.settings)*1000000/8+384000)*(end-start)*2+512*1024*1024));
    if(space.bavail*space.bsize<required)throw new Error('Free more recording-drive space to save this clip, or use a lower bitrate/duration.');
    while(true) {
      video=await this.catalog();audio=await Promise.all(this.audioProcesses.map((_,index)=>this.catalog(`audio_${index}`,'wav')));
      if(video.at(-1)?.end>=end&&audio.every(chunks=>chunks.at(-1)?.end>=end+this.audioOffset))break;
      if(!this.healthy()||Date.now()>deadline)throw new Error('Replay could not finish the current chunk. Check the source and audio devices.');await sleep(50);
    }
    const chosen=[selectChunks(video,start,end),...audio.map(chunks=>selectChunks(chunks,start+this.audioOffset,end+this.audioOffset))];
    if(this.pruning)await this.pruning;
    let job,snapshots;
    // Pin before awaiting I/O. Pruning skips only this small snapshot window, not the entire save.
    for(const chunks of chosen)for(const chunk of chunks)this.pinned.add(chunk.file);
    try {
      job=await mkdtemp(path.join(this.folder,'.camcord-sessions','clip-'));
      snapshots=[];
      for(const [index,chunks]of chosen.entries()) {
        const copies=[];for(const chunk of chunks){const target=path.join(job,path.basename(chunk.file));try{await link(chunk.file,target);}catch(error){if(!['EPERM','ENOTSUP','EOPNOTSUPP','EXDEV'].includes(error.code))throw error;await copyFile(chunk.file,target);}copies.push(target);}
        const list=path.join(job,`${index}.txt`);await writeFile(list,copies.map(concatLine).join(''),{mode:0o600});snapshots.push(list);
      }
    } catch(error){throw new Error(`${error.message}\nRecovery files: ${job}`);}
    finally{for(const chunks of chosen)for(const chunk of chunks)this.pinned.delete(chunk.file);}
    try {
      const args=['-y','-hide_banner','-loglevel','error','-xerror'];
      for(const [index,list]of snapshots.entries())args.push('-ss',String(Math.max(0,start+(index?this.audioOffset:0)-chosen[index][0].start)),'-f','concat','-safe','0','-i',list);
      args.push('-t',String(end-start),'-map','0:v:0');
      if(audio.length===1)args.push('-map','1:a:0','-af','aresample=48000:async=1:first_pts=0,apad');
      if(audio.length===2)args.push('-filter_complex','[1:a][2:a]amix=inputs=2:duration=longest:dropout_transition=2,aresample=48000:async=1:first_pts=0,apad[a]','-map','[a]');
      args.push(...encodingArgs(this.settings,{name:'libx264'}));
      if(audio.length)args.push('-c:a','aac','-b:a','192k','-ar','48000','-ac','2');else args.push('-an');
      const finished=path.join(job,'finished.mp4');args.push('-movflags','+faststart',finished);
      await run(this.ffmpeg,args,{timeout:24*60*60*1000});
      const output=path.join(this.folder,`CamCord_Clip_${new Date().toISOString().replaceAll(':','-')}_${path.basename(job)}.mp4`);
      try{await link(finished,output);}catch(error){if(!['EPERM','ENOTSUP','EOPNOTSUPP'].includes(error.code))throw error;await run(this.python,[this.helper,'publish','--source',finished,'--output',output]);}
      this.lastOutput=output;await rm(job,{recursive:true,force:true});return output;
    }catch(error){throw new Error(`${error.message}\nRecovery files: ${job}`);}
  }
}
