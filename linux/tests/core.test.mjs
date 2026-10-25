import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, mkdir, stat, rm, readdir } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { DEFAULT_SETTINGS, normalizeSettings, atomicJson, loadPreferences, writableFolder, startupEnabled, setStartup, startupCommand } from '../src/preferences.mjs';
import { newerVersion, parseRelease, verifyUpdate, sha256, Updater, ASSET_NAME } from '../src/updater.mjs';
import { encodingArgs, Recorder, concatLine } from '../src/recorder.mjs';
import { run } from '../src/process.mjs';
import { BITRATE_PRESETS, videoBitrateKbps } from '../shared/recording-quality.js';
import { FrameCache } from '../src/frames.mjs';

async function fixture(t) {const folder=await mkdtemp(path.join(os.tmpdir(),'camcord-linux-test-'));t.after(()=>rm(folder,{recursive:true,force:true}));return folder;}
function release(version='1.7.1') {return {draft:false,prerelease:false,tag_name:`v${version}`,assets:[{name:ASSET_NAME,state:'uploaded',size:3,
  browser_download_url:`https://github.com/mmaarij123/CamCord/releases/download/v${version}/${ASSET_NAME}`,digest:'sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'}]};}
test('raw frame cache handles fragmented chunks and never overwrites an active encoder frame',()=>{
  const cache=new FrameCache(4);cache.accept(Buffer.from([1,2]));assert.equal(cache.latest,null);
  cache.accept(Buffer.from([3,4,5]));const pinned=cache.latest;cache.retain(pinned);
  cache.accept(Buffer.from([6,7,8,9,10,11,12]));assert.deepEqual([...pinned],[1,2,3,4]);assert.deepEqual([...cache.latest],[9,10,11,12]);
  const buffers=new Set();for(let i=0;i<1000;i++){cache.accept(Buffer.from([20,21,22,23]));buffers.add(cache.latest);}
  assert.ok(buffers.size<=2);cache.release(pinned);cache.reset();assert.equal(cache.latest,null);
});
test('strict quality, shared bitrate recommendations and hardware flags',()=>{
  assert.equal(normalizeSettings(DEFAULT_SETTINGS,{height:720,fps:120}).fps,60);
  assert.deepEqual(normalizeSettings(DEFAULT_SETTINGS,{fps:'120',microphone:'yes',bitrateMbps:16.5}),DEFAULT_SETTINGS);
  for(const bitrateMbps of BITRATE_PRESETS) {
    const settings={...DEFAULT_SETTINGS,bitrateMbps};const args=encodingArgs(settings,{name:'libx264'});
    assert.equal(args[args.indexOf('-b:v')+1],`${videoBitrateKbps(settings)}k`);
    assert.ok(Number.parseInt(args[args.indexOf('-maxrate')+1])<=100000);
  }
  assert.ok(encodingArgs(DEFAULT_SETTINGS,{name:'h264_vaapi',device:'/dev/dri/renderD128'}).includes('format=nv12,hwupload'));
});
test('Unicode settings persist atomically without stale capture handles',async t=>{
  const root=await fixture(t),file=path.join(root,'config','settings.json'),folder=path.join(root,"ریکارڈنگ 🎬 O'Brien");
  await writableFolder(folder);
  await atomicJson(file,{settings:{...DEFAULT_SETTINGS,bitrateMbps:100,microphone:true,captureTarget:{id:42},replaySeconds:300,replayHotkey:'Ctrl+Shift+F8'},replay:{enabled:true},outputFolder:folder});
  const loaded=await loadPreferences(file,'fallback');assert.equal(loaded.outputFolder,folder);assert.equal(loaded.settings.bitrateMbps,100);
  assert.equal(loaded.settings.captureTarget,undefined);
  assert.equal(loaded.settings.replaySeconds,undefined);assert.equal(loaded.settings.replayHotkey,undefined);assert.equal(loaded.replay,undefined);
  assert.rejects(writableFolder('relative-location'));
  await writeFile(file,'invalid JSON');assert.deepEqual((await loadPreferences(file,folder)).settings,DEFAULT_SETTINGS);
});
test('login startup is opt-in, quoted, launch-only and isolated',async t=>{
  const root=await fixture(t),file=path.join(root,'autostart','camcord.desktop'),exe='/opt/CamCord App/camcord';
  assert.equal(await startupEnabled(file,exe),false);await setStartup(file,exe,true);
  assert.equal(await startupEnabled(file,exe),true);assert.ok((await readFile(file,'utf8')).includes('"/opt/CamCord App/camcord" --startup'));
  assert.throws(()=>startupCommand('/opt/app\nExec=bad'));
  await setStartup(file,exe,false);assert.equal(await startupEnabled(file,exe),false);
});
test('Debian release metadata is platform-specific, stable and downgrade-safe',()=>{
  assert.ok(newerVersion('1.7.1','1.7.0'));assert.equal(newerVersion('1.6.2','1.7.0'),false);assert.equal(newerVersion('1.7.1-beta','1.7.0'),false);
  assert.equal(parseRelease(release(),'1.7.0').version,'1.7.1');assert.equal(parseRelease(release('1.7.0'),'1.7.0'),null);
  for(const mutate of [r=>r.prerelease=true,r=>r.assets[0].digest=null,r=>r.assets[0].browser_download_url='https://example.com/setup.deb',
    r=>r.assets.push({...r.assets[0]}),r=>r.assets[0].size=1024**3,r=>r.assets[0].name='CamCord-Setup.exe']) {
    const candidate=release();mutate(candidate);assert.throws(()=>parseRelease(candidate,'1.7.0'));
  }
});
test('download hash/cache verification rejects tampering',async t=>{
  const root=await fixture(t),info=parseRelease(release(),'1.7.0'),cache=path.join(root,info.version);await mkdir(cache);
  const file=path.join(cache,ASSET_NAME);await writeFile(file,'abc');await verifyUpdate(info,file);assert.equal(await sha256(file),info.sha256);
  const updater=new Updater(root,{fetcher:()=>{throw new Error('Cache should not request the network');}});
  assert.equal(await updater.download(info),file);await writeFile(file,'bad');await assert.rejects(verifyUpdate(info,file));
  await assert.rejects(updater.download({...info,url:'https://example.com/untrusted.deb'}),/Invalid update metadata/);
  await assert.rejects(updater.download({...info,size:1024**3}),/Invalid update metadata/);
});
test('streamed updates verify bytes, reject excess data and clean cancelled partial downloads',async t=>{
  const root=await fixture(t),info=parseRelease(release(),'1.7.0'),progress=[];
  const updater=new Updater(root,{onProgress:value=>progress.push(value),fetcher:async()=>({ok:true,url:info.url,
    body:(async function*(){yield Buffer.from('a');yield Buffer.from('bc');})()})});
  const file=await updater.download(info);assert.equal(await readFile(file,'utf8'),'abc');assert.equal(progress.at(-1),100);
  await rm(file);
  updater.fetcher=async()=>({ok:true,url:info.url,body:(async function*(){yield Buffer.from('abcd');})()});
  await assert.rejects(updater.download(info),/expected size/);
  updater.fetcher=async()=>({ok:true,url:info.url,body:(async function*(){yield Buffer.from('bad');})()});
  await assert.rejects(updater.download(info),/SHA-256/);
  updater.fetcher=async()=>({ok:true,url:info.url,body:(async function*(){yield Buffer.from('abc');updater.cancel();})()});
  await assert.rejects(updater.download(info),/cancelled/);
  assert.deepEqual(await readdir(path.join(root,info.version)),[]);
});

const ffmpeg=process.env.FFMPEG_PATH || (process.platform==='win32'?fileURLToPath(new URL('../../third_party/ffmpeg/ffmpeg.exe',import.meta.url)):'ffmpeg');
test('paused segment publication, apostrophe paths, collisions and corrupt recovery',async t=>{
  const root=await fixture(t),folder=path.join(root,"O'Brien"),output=path.join(root,'finished.mp4');await mkdir(folder);
  const first=path.join(folder,'one.mp4'),second=path.join(folder,'two.mp4');
  for(const [index,file] of [first,second].entries()) await run(ffmpeg,['-y','-hide_banner','-loglevel','error','-f','lavfi','-i',`color=c=${index?'green':'blue'}:s=160x90:r=15`,
    '-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t','1','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-c:a','aac',file]);
  const recorder=new Recorder({ffmpeg});Object.assign(recorder,{session:folder,output,segments:[first,second]});await recorder.finalize();assert.ok((await stat(output)).size>0);
  await run(ffmpeg,['-hide_banner','-loglevel','error','-xerror','-i',output,'-f','null','-']);
  await assert.rejects(recorder.finalize(),{code:'EEXIST'});assert.ok((await stat(path.join(folder,'finished.mp4'))).size>0);
  await writeFile(first,'corrupt media');await assert.rejects(recorder.finalize());assert.equal(await readFile(first,'utf8'),'corrupt media');
  assert.ok(concatLine(first).includes("'\\''"));
});
