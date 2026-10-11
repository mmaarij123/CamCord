import electron from 'electron';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { readFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { Recorder } from './recorder.mjs';
import { loadPreferences, atomicJson, normalizeSettings, writableFolder, startupEnabled, setStartup } from './preferences.mjs';
import { Updater, verifyUpdate } from './updater.mjs';

const {app,BrowserWindow,ipcMain,dialog,protocol,net,session,shell,powerSaveBlocker}=electron;
const root=fileURLToPath(new URL('../',import.meta.url)), uiRoot=path.join(root,'ui');
const version=JSON.parse(await readFile(path.join(root,'package.json'),'utf8')).version;
const APP_URL='camcord://app/index.html';
const smoke=process.env.CAMCORD_SMOKE_TEST==='1' && process.argv.includes('--smoke-test');
app.setName('CamCord');
app.setDesktopName('camcord.desktop');
if(smoke) {app.commandLine.appendSwitch('no-sandbox');app.setPath('userData',process.env.CAMCORD_TEST_DATA);}
protocol.registerSchemesAsPrivileged([{scheme:'camcord',privileges:{standard:true,secure:true,supportFetchAPI:true}}]);
if(!app.requestSingleInstanceLock()) app.quit();
let window,ready=false,pending=false,folderDialog=false,closing=false,allowClose=false,timer,blocker;
let updateBusy=false,updateManual=false,updateInfo,updateFile,update={status:'idle',message:'Check for updates.',version:'',progress:0};
let nextCheck=Date.now()+5000;
const portal=process.env.XDG_SESSION_TYPE==='wayland' || Boolean(process.env.WAYLAND_DISPLAY);
const recorder=new Recorder({onFailure:message=>{
  if(recorder.capture?.exited)source={...source,ready:false};
  if(!pending && ['recording','paused'].includes(recorder.state)) action('saving',async()=>{
    await recorder.stop();return {severity:'warning',text:`${message} The completed portion was saved.`};
  });
}});
let preferences,preferencesFile,startupFile,startup=false,sources=[],source={kind:'display',id:'',label:'Choose a display',ready:false,width:0,height:0};
function trusted(event) {return window && event.sender===window.webContents && event.senderFrame===window.webContents.mainFrame && event.senderFrame.url===APP_URL;}
function send(notice) {
  if(!ready || !window || window.isDestroyed()) return;
  const state={type:'state',platform:'linux',state:pending || recorder.state,elapsedSeconds:recorder.elapsedSeconds(),settings:preferences.settings,
    appVersion:version,startupEnabled:startup,update,encoder:recorder.encoder?.label||'',outputFolder:preferences.outputFolder,
    lastOutput:recorder.lastOutput?path.basename(recorder.lastOutput):'',captureExcluded:false,cursorHidden:true,sourceDescription:portal?'Your desktop asks permission and chooses the source when recording starts.':'',
    captureSource:source,sources:sources.map(({kind,id,label,primary})=>({kind,id,label,primary})),...(notice?{notice}:{})};
  window.webContents.send('camcord-state',state);
}
async function saveSettings() {await atomicJson(preferencesFile,preferences);}
function holdPower() {if(blocker===undefined)blocker=powerSaveBlocker.start('prevent-display-sleep');}
function releasePower() {if(blocker!==undefined){powerSaveBlocker.stop(blocker);blocker=undefined;}}
async function action(state,operation) {
  if(pending || folderDialog) {send();return;}
  pending=state;send();
  try {const notice=await operation();send(notice);} catch(error) {send({severity:'error',text:error.message});}
  finally {
    pending=false;
    if(recorder.state==='idle')releasePower();
    send();
    if(closing) {
      if(recorder.state==='idle'){allowClose=true;window.close();}
      else action('saving',async()=>{await recorder.stop();});
    }
  }
}
async function refreshSources() {
  sources=await recorder.sources(portal,process.pid);
  if(source.id && !sources.some(entry=>entry.id===source.id)) source={...source,ready:false};
}
function select(entry) {source={...entry,ready:true,width:entry.width||0,height:entry.height||0};}
async function updateAction(type,manual=true) {
  if(updateBusy || closing)return;
  if(type==='installUpdate' && (pending || folderDialog || recorder.state!=='idle' || update.status!=='ready')) {send({severity:'info',text:'Stop and save your recording before installing an update.'});return;}
  updateBusy=true;updateManual=manual;
  try {
    if(type==='checkUpdates') {
      update={status:'checking',message:'Checking for updates…',version:'',progress:0};send();nextCheck=Date.now()+6*60*60*1000;
      updateInfo=await updater.check(version);
      update={status:updateInfo?'available':'idle',message:updateInfo?'A Debian update is available.':"You’re up to date.",version:updateInfo?.version||'',progress:0};
    } else if(type==='downloadUpdate') {
      if(!updateInfo)return;
      update={...update,status:'downloading',message:'Downloading verified Debian update… You can keep recording.',progress:0};send();
      updateFile=await updater.download(updateInfo);
      update={...update,status:'ready',message:'Update ready. Install when you’ve finished recording.'};
    } else if(type==='installUpdate') {
      update={...update,status:'installing',message:'Your desktop will ask permission to install the Debian update…'};send();
      await verifyUpdate(updateInfo,updateFile);
      await new Promise((resolve,reject)=>{
        const installer=spawn('/usr/bin/pkexec',['/usr/bin/apt-get','install','-y','--',updateFile],{stdio:['ignore','pipe','pipe']});
        let details='';installer.stderr.on('data',bytes=>{details=(details+bytes.toString()).slice(-2000);});installer.stdout.resume();
        installer.once('error',reject);installer.once('close',code=>code===0?resolve():reject(new Error('Installation was cancelled or failed. Your app and recordings remain open. '+details)));
      });
      app.relaunch();allowClose=true;app.quit();return;
    }
  } catch(error) {update={...update,status:type==='installUpdate'?'ready':'error',message:error.message};}
  finally {updateBusy=false;send();}
  if(type==='checkUpdates' && updateInfo && (manual || preferences.settings.autoCheckUpdates) && !closing) updateAction('downloadUpdate',manual);
}
let updater;
async function command(message) {
  if(typeof message!=='string' || message.length>4096)return;
  let data;try {data=JSON.parse(message);} catch{return;}
  if(!data || typeof data.type!=='string' || Array.isArray(data))return;
  if(data.type==='ready'){ready=true;send();return;}
  if(closing || update.status==='installing') {send();return;}
  if(['checkUpdates','downloadUpdate','installUpdate'].includes(data.type)) {if(data.type!=='downloadUpdate'||update.status==='available')await updateAction(data.type);return;}
  if(data.type==='openOutput'){await shell.openPath(preferences.outputFolder);return;}
  if(pending || folderDialog){send();return;}
  if(data.type==='openLast'){if(recorder.lastOutput)shell.showItemInFolder(recorder.lastOutput);return;}
  if(data.type==='stop' && ['recording','paused'].includes(recorder.state)){await action('saving',async()=>{await recorder.stop();return{severity:'success',text:'Recording saved successfully.'};});return;}
  if(data.type==='pause') {
    if(recorder.state==='recording')await action('pausing',async()=>{await recorder.pause();});
    else if(recorder.state==='paused')await action('resuming',async()=>{await recorder.resume();});
    return;
  }
  if(recorder.state!=='idle'){send();return;}
  if(data.type==='settings') {
    const previous=preferences.settings.autoCheckUpdates;
    preferences.settings=normalizeSettings(preferences.settings,data.settings);await saveSettings();
    if(previous!==preferences.settings.autoCheckUpdates) {nextCheck=Date.now();if(!preferences.settings.autoCheckUpdates && !updateManual)updater.cancel();}
  } else if(data.type==='startup' && typeof data.enabled==='boolean') {
    await setStartup(startupFile,process.execPath,data.enabled);startup=await startupEnabled(startupFile,process.execPath);
  } else if(data.type==='sourceMode' && ['display','window'].includes(data.kind)) {
    await refreshSources();
    const entry=sources.find(item=>item.kind===data.kind);
    if(data.kind==='display' || portal) {if(entry)select(entry);else source={kind:data.kind,id:'',label:'Choose a display',ready:false};}
    else source={kind:'window',id:'',label:'Choose a window',ready:false,width:0,height:0};
  } else if(data.type==='selectSource') {
    const entry=sources.find(item=>item.id===data.id && item.kind===source.kind);
    if(entry)select(entry);else send({severity:'warning',text:'That source is unavailable. Refresh and choose it again.'});
  } else if(data.type==='refreshSources') {await refreshSources();}
  else if(data.type==='chooseOutput') {
    folderDialog=true;
    try {
      const result=await dialog.showOpenDialog(window,{title:'Choose where CamCord saves recordings',defaultPath:preferences.outputFolder,properties:['openDirectory','createDirectory']});
      if(!result.canceled && !closing){await writableFolder(result.filePaths[0]);preferences.outputFolder=result.filePaths[0];await saveSettings();send({severity:'success',text:'New recordings will be saved in the selected folder.'});}
    } finally {folderDialog=false;if(closing){allowClose=true;window.close();}}
  } else if(data.type==='start') {
    if(!source.ready){send({severity:'warning',text:'Choose an available source before recording.'});return;}
    await action('starting',async()=>{
      preferences.settings=normalizeSettings(preferences.settings,data.settings);holdPower();
      if(source.kind==='display')window.minimize();
      try {await recorder.start(preferences.settings,preferences.outputFolder,source,portal);} catch(error) {window.restore();throw error;}
      preferences.settings=normalizeSettings(preferences.settings,recorder.settings);await saveSettings();
    });
  }
  send();
}

app.on('second-instance',(_,arguments_)=>{if(!arguments_.includes('--startup') && window){if(window.isMinimized())window.restore();window.show();window.focus();}});
app.whenReady().then(async()=>{
  preferencesFile=path.join(app.getPath('userData'),'settings.json');
  preferences=await loadPreferences(preferencesFile,path.join(app.getPath('videos'),'CamCord Captures'));
  startupFile=path.join(process.env.XDG_CONFIG_HOME || path.join(app.getPath('home'),'.config'),'autostart','camcord.desktop');
  startup=await startupEnabled(startupFile,process.execPath);
  updater=new Updater(path.join(app.getPath('userData'),'Updates'),{onProgress:progress=>{update={...update,progress};send();}});
  protocol.handle('camcord',async request=>{
    const url=new URL(request.url);
    if(url.hostname!=='app')return new Response('Forbidden',{status:403});
    const file=path.resolve(uiRoot,`.${decodeURIComponent(url.pathname)}`);
    if(!file.startsWith(uiRoot+path.sep))return new Response('Forbidden',{status:403});
    const response=await net.fetch(pathToFileURL(file).href);
    const headers=new Headers(response.headers);
    headers.set('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'self'; object-src 'none'; frame-src 'none'");
    headers.set('Cache-Control','no-store');return new Response(response.body,{status:response.status,headers});
  });
  session.defaultSession.setPermissionRequestHandler((_,permission,callback)=>callback(false));
  session.defaultSession.setPermissionCheckHandler(()=>false);
  window=new BrowserWindow({width:1040,height:740,minWidth:620,minHeight:600,title:'CamCord',backgroundColor:'#0c0f14',icon:path.join(uiRoot,'camcord-logo.png'),
    show:false,autoHideMenuBar:true,webPreferences:{preload:path.join(root,'src','preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true,backgroundThrottling:false}});
  window.webContents.setWindowOpenHandler(()=>({action:'deny'}));
  window.webContents.on('will-navigate',(event,url)=>{if(url!==APP_URL)event.preventDefault();});
  ipcMain.on('camcord-command',(event,message)=>{if(trusted(event))command(message).catch(error=>send({severity:'error',text:error.message}));});
  window.on('close',event=>{
    if(allowClose)return;
    if(update.status==='installing'){event.preventDefault();return;}
    if(pending || folderDialog || recorder.state!=='idle') {
      event.preventDefault();closing=true;updater.cancel();
      if(pending==='starting')recorder.capture?.child.kill('SIGTERM');
      if(!pending && !folderDialog)action('saving',async()=>{await recorder.stop();});
      send({severity:'info',text:folderDialog?'Close the folder selector to finish closing CamCord.':'CamCord will close after your recording finishes saving.'});
    }
  });
  window.once('ready-to-show',()=>{window.show();if(process.argv.includes('--startup'))window.minimize();});
  await refreshSources().catch(error=>{send({severity:'error',text:error.message});});
  const initial=sources.find(item=>item.kind==='display');if(initial)select(initial);
  await window.loadURL(APP_URL);
  timer=setInterval(()=>{
    send();
    if(!pending && !closing && ['recording','paused'].includes(recorder.state) && recorder.capture?.exited) {
      source={...source,ready:false};
      action('saving',async()=>{await recorder.stop();return{severity:'warning',text:'The capture source ended. The completed portion was saved.'};});
    }
    if(!smoke && !closing && ready && preferences.settings.autoCheckUpdates && !updateBusy && update.status!=='ready' && Date.now()>=nextCheck)updateAction('checkUpdates',false);
  },250);
  if(smoke) {
    try {
      const result=await window.webContents.executeJavaScript(`new Promise((resolve,reject)=>{let count=0;const t=setInterval(()=>{const buttons=[...document.querySelectorAll('.source-mode-picker button')];if(buttons.length===2){clearInterval(t);resolve({buttons:buttons.map(b=>b.textContent),text:document.body.innerText});}else if(++count>100){clearInterval(t);reject(new Error('React UI did not load'));}},50);})`);
      if(result.buttons.join(',')!=='Screen,Window' || !result.text.includes('Launch at login'))throw new Error('Linux UI/platform controls mismatch.');
      console.log('PASS: packaged React UI, Screen/Window controls, Linux startup copy and isolated bridge');app.exit(0);
    } catch(error){console.error(error);app.exit(1);}
  }
}).catch(error=>{console.error(error);app.exit(1);});
app.on('window-all-closed',()=>{clearInterval(timer);updater?.cancel();releasePower();app.quit();});
