import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, readFile, rm, readdir } from 'node:fs/promises';
import { once } from 'node:events';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Recorder, HELPER } from '../src/recorder.mjs';
import { run } from '../src/process.mjs';
import { DEFAULT_SETTINGS } from '../src/preferences.mjs';
const enabled=process.env.CAMCORD_DESKTOP_TEST==='1';
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
test('real X11 window updates under occlusion; pause, audio, source loss and recovery', {skip:!enabled,timeout:120000},async t=>{
  const root=await mkdtemp(path.join(os.tmpdir(),'camcord-desktop-test-'));
  const active=[];
  t.after(async()=>{
    for(const recorder of active) {await recorder.abortSource();await recorder.abortEncoder();}
    for(const item of await readdir(path.join(root,'.camcord-sessions')).catch(()=>[])) {
      const folder=path.join(root,'.camcord-sessions',item);
      for(const file of (await readdir(folder)).filter(name=>name.endsWith('.log')))console.log(`Fixture ${file}: ${(await readFile(path.join(folder,file),'utf8')).slice(-4000)}`);
    }
    await rm(root,{recursive:true,force:true});
  });
  const fixture=spawn('python3',[fileURLToPath(new URL('./x11_fixture.py',import.meta.url))],{stdio:['pipe','pipe','inherit']});
  t.after(()=>fixture.kill('SIGTERM'));
  const [first]=await once(fixture.stdout,'data');const id=first.toString().trim();assert.match(id,/^window:/);
  await sleep(500);
  const recorder=new Recorder();active.push(recorder);const sources=await recorder.sources(false,process.pid);assert.ok(sources.find(item=>item.id===id));
  const source=sources.find(item=>item.id===id);
  const settings={...DEFAULT_SETTINGS,height:480,fps:30,bitrateMbps:8,systemAudio:true,microphone:true};
  await recorder.start(settings,root,source,false);await sleep(500);
  fixture.stdin.write('cover\ngreen\n');await sleep(1200);
  await recorder.pause();assert.equal(recorder.state,'paused');
  const pixel=path.join(root,'last.rgb');
  await run('ffmpeg',['-y','-hide_banner','-loglevel','error','-sseof','-0.2','-i',recorder.segments[0],'-frames:v','1','-vf','scale=1:1','-pix_fmt','rgb24','-f','rawvideo',pixel]);
  const bytes=await readFile(pixel);assert.ok(bytes[1]>100 && bytes[0]<60 && bytes[2]<60,`Occluded window did not update: ${[...bytes]}`);
  await sleep(300);await recorder.resume();await sleep(500);const saved=await recorder.stop();assert.equal(recorder.state,'idle');
  const metadata=JSON.parse(await run('ffprobe',['-v','error','-show_streams','-show_format','-of','json',saved]));
  assert.ok(metadata.streams.find(item=>item.codec_type==='audio'));assert.ok(Number(metadata.format.duration)>1);
  await run('ffmpeg',['-hide_banner','-loglevel','error','-xerror','-i',saved,'-f','null','-']);
  const videoOnly=new Recorder();active.push(videoOnly);await videoOnly.start({...settings,systemAudio:false,microphone:false},root,source,false);
  fixture.stdin.write('close\n');await sleep(300);await videoOnly.stop();
  await assert.rejects(run('python3',[HELPER,'capture','--kind','window','--id',id,'--width','854','--height','480','--fps','30']));
  fixture.stdin.write('quit\n');
});
