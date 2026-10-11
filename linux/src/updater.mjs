import { mkdir, open, rename, stat, unlink } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createHash, randomUUID } from 'node:crypto';
import path from 'node:path';

export const ASSET_NAME='CamCord-Debian-amd64.deb';
const RELEASE_BASE='https://github.com/mmaarij123/CamCord/releases/download/';
export function versionParts(value) {
  if(typeof value!=='string' || !/^v?\d+\.\d+\.\d+$/.test(value)) return null;
  const parts=value.replace(/^v/,'').split('.').map(Number);
  return parts.every(Number.isSafeInteger)?parts:null;
}
export function newerVersion(left,right) {
  const a=versionParts(left),b=versionParts(right);if(!a || !b)return false;
  for(let i=0;i<3;i++) if(a[i]!==b[i])return a[i]>b[i];return false;
}
export function parseRelease(release,current) {
  if(!release || release.draft!==false || release.prerelease!==false || !versionParts(release.tag_name) || !release.tag_name.startsWith('v')) throw new Error('No valid stable release is available.');
  if(!newerVersion(release.tag_name,current)) return null;
  const matches=Array.isArray(release.assets)?release.assets.filter(asset=>asset.name===ASSET_NAME):[];
  if(matches.length!==1) throw new Error('The release does not contain a complete Debian package yet. Try again later.');
  const asset=matches[0],version=release.tag_name.slice(1);
  if(asset.state!=='uploaded' || !Number.isSafeInteger(asset.size) || asset.size<=0 || asset.size>512*1024*1024 ||
    asset.browser_download_url!==`${RELEASE_BASE}v${version}/${ASSET_NAME}` || !/^sha256:[a-f0-9]{64}$/.test(asset.digest)) throw new Error('The Debian update metadata could not be verified.');
  return {version,size:asset.size,url:asset.browser_download_url,sha256:asset.digest.slice(7)};
}
export async function sha256(file) {
  const hash=createHash('sha256');for await(const bytes of createReadStream(file)) hash.update(bytes);return hash.digest('hex');
}
export async function verifyUpdate(info,file) {
  if(!info || !versionParts(info.version) || info.url!==`${RELEASE_BASE}v${info.version}/${ASSET_NAME}` || !/^[a-f0-9]{64}$/.test(info.sha256) ||
    !Number.isSafeInteger(info.size) || info.size<=0 || info.size>512*1024*1024) throw new Error('Invalid update metadata.');
  const data=await stat(file);
  if(!data.isFile() || data.size!==info.size || await sha256(file)!==info.sha256) throw new Error('The downloaded package failed SHA-256 verification. Download it again.');
}
export class Updater {
  constructor(folder,{fetcher=fetch,onProgress=()=>{}}={}) {this.folder=folder;this.fetcher=fetcher;this.onProgress=onProgress;}
  cancel() {this.controller?.abort();}
  async check(current) {
    this.controller=new AbortController();
    const response=await this.fetcher('https://api.github.com/repos/mmaarij123/CamCord/releases/latest',{
      headers:{'User-Agent':`CamCord/${current}`,'Accept':'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28'},
      signal:AbortSignal.any([this.controller.signal,AbortSignal.timeout(15000)])});
    if(!response.ok) throw new Error('Could not check for updates. Check your connection or try again later.');
    let text='';for await(const bytes of response.body) {text+=Buffer.from(bytes).toString('utf8');if(Buffer.byteLength(text)>2*1024*1024)throw new Error('Update response too large.');}
    return parseRelease(JSON.parse(text),current);
  }
  async download(info) {
    this.controller=new AbortController();
    const folder=path.join(this.folder,info.version);
    if(!versionParts(info.version) || !/^\d+\.\d+\.\d+$/.test(info.version))throw new Error('Invalid update version.');
    await mkdir(folder,{recursive:true,mode:0o700});const final=path.join(folder,ASSET_NAME);
    try {await verifyUpdate(info,final);this.onProgress(100);return final;} catch {}
    const temporary=path.join(folder,`${ASSET_NAME}.${randomUUID()}.partial`);let handle;
    try {
      const response=await this.fetcher(info.url,{signal:AbortSignal.any([this.controller.signal,AbortSignal.timeout(30*60*1000)])});
      if(!response.ok || !response.url.startsWith('https://'))throw new Error('The Debian package could not be downloaded.');
      handle=await open(temporary,'wx',0o600);let total=0;
      for await(const bytes of response.body) {
        total+=bytes.length;if(total>info.size)throw new Error('Update exceeded its expected size.');
        await handle.writeFile(bytes);this.onProgress(Math.min(99,Math.floor(total*100/info.size)));
      }
      await handle.sync();await handle.close();handle=null;
      await verifyUpdate(info,temporary);
      if(this.controller.signal.aborted)throw new Error('Update cancelled.');
      await rename(temporary,final);this.onProgress(100);return final;
    } finally {if(handle)await handle.close();await unlink(temporary).catch(()=>{});}
  }
}
