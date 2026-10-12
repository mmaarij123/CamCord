import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, readFile, writeFile, link, readdir, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { performance } from 'node:perf_hooks';
import { Replay, parseCatalog, selectChunks } from '../src/replay.mjs';
import { run } from '../src/process.mjs';
import { DEFAULT_SETTINGS } from '../src/preferences.mjs';
import { REPLAY_DURATIONS } from '../shared/replay-settings.js';
const ffmpeg=process.env.FFMPEG_PATH||(process.platform==='win32'?fileURLToPath(new URL('../../third_party/ffmpeg/ffmpeg.exe',import.meta.url)):'ffmpeg');
test('replay catalog rejects unsafe entries and incomplete/gapped snapshots',()=>{
  const chunks=parseCatalog('"/tmp/a,folder/video_000000001.mkv",2,4\nvideo_000000002.mkv,4,6\n/etc/passwd,0,8\nvideo_000000003.mkv,6,NaN\n','/owned','video','mkv');
  assert.equal(chunks.length,2);assert.equal(chunks[0].file,path.join('/owned','video_000000001.mkv'));assert.equal(selectChunks(chunks,3,5).length,2);
  assert.throws(()=>selectChunks(chunks,0,5));assert.throws(()=>selectChunks(chunks,3,7));assert.throws(()=>selectChunks([{...chunks[0],end:3},chunks[1]],2,5));
});
test('all replay durations trim to the hotkey time, audio mixes, unique clips, corruption recovery and bounded pruning', {timeout:180000},async t=>{
  const root=await mkdtemp(path.join(os.tmpdir(),'camcord-replay-test-'));t.after(()=>rm(root,{recursive:true,force:true}));
  const session=path.join(root,'.camcord-sessions','buffer');await mkdir(session,{recursive:true});
  await run(ffmpeg,['-y','-hide_banner','-loglevel','error','-f','lavfi','-i','color=c=blue:s=160x90:r=15','-vf',"drawbox=color=red:t=fill:enable='gte(t,308)'",'-t','310','-an','-c:v','libx264','-preset','ultrafast','-g','30','-bf','0',
    '-f','segment','-segment_time','2','-segment_format','matroska','-reset_timestamps','1','-segment_list_type','csv','-segment_list_size','162','-segment_list',path.join(session,'video.csv'),path.join(session,'video_%09d.mkv')],{timeout:60000});
  const tone=path.join(root,'tone.wav');await run(ffmpeg,['-y','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=660:sample_rate=48000','-t','2','-c:a','pcm_s16le','-ac','2','-rf64','always',tone]);
  for(let track=0;track<2;track++) {
    const lines=[];for(let index=0;index<155;index++){const name=`audio_${track}_${String(index).padStart(9,'0')}.wav`;await link(tone,path.join(session,name));lines.push(`${name},${index*2},${index*2+2}`);}
    await writeFile(path.join(session,`audio_${track}.csv`),lines.join('\n')+'\n');
  }
  const replay=new Replay({ffmpeg});Object.assign(replay,{folder:root,session,enabled:true,state:'recording',segmentAt:performance.now()-307400,audioOffset:0,audioProcesses:[{},{}],settings:{...DEFAULT_SETTINGS,height:480,fps:15,bitrateMbps:2,microphone:true}});
  const outputs=[];
  for(const seconds of [...REPLAY_DURATIONS].reverse()) {
    replay.settings.replaySeconds=seconds;const saved=await replay.saveClip(307.4);outputs.push(saved);
    const pcm=path.join(root,'duration.pcm');await run(ffmpeg,['-y','-hide_banner','-loglevel','error','-xerror','-i',saved,'-map','0:a:0','-ar','8000','-ac','1','-f','s16le',pcm]);
    const data=await readFile(pcm);assert.ok(Math.abs(data.length/16000-seconds)<0.15,`${seconds}-second clip duration mismatch`);
    let peak=0;for(let index=0;index<data.length;index+=2)peak=Math.max(peak,Math.abs(data.readInt16LE(index)));assert.ok(peak>300,'Clip audio is silent');
  }
  assert.equal(new Set(outputs).size,REPLAY_DURATIONS.length);
  const pixel=path.join(root,'last.rgb');await run(ffmpeg,['-y','-hide_banner','-loglevel','error','-sseof','-0.2','-i',outputs.at(-1),'-frames:v','1','-vf','scale=1:1','-pix_fmt','rgb24','-f','rawvideo',pixel]);
  const rgb=await readFile(pixel);assert.ok(rgb[2]>150&&rgb[0]<60,'Clip includes frames after the hotkey time');
  await replay.prune();const names=await readdir(session);assert.ok(names.filter(name=>name.endsWith('.mkv')).length<30,'Replay video did not expire');
  assert.ok(names.filter(name=>name.endsWith('.wav')).length<60,'Replay audio did not expire');
  await writeFile(path.join(session,'video_000000150.mkv'),'corrupt');await assert.rejects(replay.saveClip(307.4),/Recovery files/);
  assert.ok((await readdir(path.join(root,'.camcord-sessions'))).some(name=>name.startsWith('clip-')));assert.equal(replay.enabled,true);
});
