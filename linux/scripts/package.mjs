import { cp, mkdir, mkdtemp, writeFile, chmod, readFile, stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { packager } from '@electron/packager';
import './prepare-shared.mjs';

if(process.platform!=='linux' || process.arch!=='x64')throw new Error('Build the Debian package on Linux x64 (use the supplied Dockerfile).');
const root=fileURLToPath(new URL('../../',import.meta.url)),linux=path.join(root,'linux'),out=path.join(linux,'out');
const metadata=JSON.parse(await readFile(path.join(linux,'package.json'),'utf8'));
const uiMetadata=JSON.parse(await readFile(path.join(root,'ui','package.json'),'utf8'));
if(metadata.version!==uiMetadata.version)throw new Error('Linux and shared UI versions differ.');
const command=(exe,args,cwd=root)=>new Promise((resolve,reject)=>{
  const child=spawn(exe,args,{cwd,stdio:'inherit'});child.once('error',reject);child.once('close',code=>code===0?resolve():reject(new Error(`${exe} failed (${code})`)));
});
await command('npm',['ci','--no-audit','--no-fund'],path.join(root,'ui'));
await command('npm',['run','build'],path.join(root,'ui'));
await mkdir(out,{recursive:true});const stage=await mkdtemp(path.join(out,'app-build-'));
for(const directory of ['src','shared'])await cp(path.join(linux,directory),path.join(stage,directory),{recursive:true});
await cp(path.join(linux,'package.json'),path.join(stage,'package.json'));
await cp(path.join(root,'ui','dist'),path.join(stage,'ui'),{recursive:true});
for(const file of ['LICENSE','README.md','THIRD_PARTY_NOTICES.md'])await cp(path.join(root,file),path.join(stage,file));
await command('node',[path.join(root,'scripts','collect-licenses.mjs'),root,path.join(stage,'licenses')]);
const [runtime]=await packager({dir:stage,name:'camcord',platform:'linux',arch:'x64',electronVersion:metadata.electronVersion,
  out:path.join(out,'runtime'),overwrite:true,prune:true,asar:false,appVersion:metadata.version,appCopyright:'CamCord contributors',quiet:true});
const deb=await mkdtemp(path.join(out,'deb-build-'));
await mkdir(path.join(deb,'opt'),{recursive:true});await cp(runtime,path.join(deb,'opt','camcord'),{recursive:true});
await chmod(path.join(deb,'opt','camcord','chrome-sandbox'),0o4755);
await mkdir(path.join(deb,'DEBIAN'));await mkdir(path.join(deb,'usr','share','applications'),{recursive:true});
await mkdir(path.join(deb,'usr','share','icons','hicolor','128x128','apps'),{recursive:true});
await cp(path.join(root,'ui','public','camcord-logo.png'),path.join(deb,'usr','share','icons','hicolor','128x128','apps','camcord.png'));
await writeFile(path.join(deb,'usr','share','applications','camcord.desktop'),`[Desktop Entry]\nType=Application\nName=CamCord\nComment=Screen and window recorder\nExec=/opt/camcord/camcord\nTryExec=/opt/camcord/camcord\nIcon=camcord\nTerminal=false\nCategories=AudioVideo;Recorder;\nStartupWMClass=camcord\n`);
const depends=['libc6 (>= 2.36)','libgtk-3-0t64 | libgtk-3-0','libnss3','libxss1','libasound2t64 | libasound2','libgbm1','libdrm2','libx11-6','libx11-xcb1','libxcomposite1','libxrandr2',
  'libatk-bridge2.0-0t64 | libatk-bridge2.0-0','libatspi2.0-0t64 | libatspi2.0-0','libxkbcommon0','libcups2t64 | libcups2','ffmpeg (>= 5.1)',
  'pulseaudio-utils','python3 (>= 3.11)','python3-gi','gir1.2-gstreamer-1.0','gstreamer1.0-plugins-base','gstreamer1.0-plugins-good','gstreamer1.0-pipewire','xdg-desktop-portal','xdg-utils','pkexec'];
await writeFile(path.join(deb,'DEBIAN','control'),`Package: camcord\nVersion: ${metadata.version}-1\nSection: video\nPriority: optional\nArchitecture: amd64\nMaintainer: CamCord contributors <mmaarij123@users.noreply.github.com>\nHomepage: https://github.com/mmaarij123/CamCord\nDepends: ${depends.join(', ')}\nRecommends: pipewire-pulse | pulseaudio\nSuggests: xdg-desktop-portal-gnome | xdg-desktop-portal-kde | xdg-desktop-portal-wlr\nDescription: CamCord screen and window recorder\n Shared React interface with H.264 recording, audio, bitrate controls,\n pause/resume, custom save locations, verified updates and login startup.\n`);
const packageFile=path.join(out,'CamCord-Debian-amd64.deb');
await command('dpkg-deb',['--root-owner-group','-Zxz','-z3','--build',deb,packageFile]);
console.log(`Debian package ready: ${packageFile} (${(await stat(packageFile)).size} bytes)`);
