import { mkdir, readFile, rename, writeFile, unlink, stat, open, statfs } from 'node:fs/promises';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { normalizeBitrate } from '../shared/recording-quality.js';
import { normalizeReplaySeconds, normalizeReplayHotkey } from '../shared/replay-settings.js';

export const DEFAULT_SETTINGS = Object.freeze({height:1080, fps:60, bitrateMbps:0, systemAudio:true, microphone:false, autoCheckUpdates:true, replaySeconds:60, replayHotkey:'Ctrl+Shift+F8'});
export function normalizeSettings(current, changes = {}) {
  const next = { ...current };
  if (!changes || typeof changes !== 'object' || Array.isArray(changes)) return next;
  if (Number.isInteger(changes.height)) next.height = [480,720,1080].includes(changes.height) ? changes.height : 1080;
  if (Number.isInteger(changes.fps)) next.fps = [15,30,60,120].includes(changes.fps) ? changes.fps : 60;
  if (next.height !== 1080 && next.fps === 120) next.fps = 60;
  if (Number.isInteger(changes.bitrateMbps)) next.bitrateMbps = normalizeBitrate(changes.bitrateMbps);
  if (Number.isInteger(changes.replaySeconds)) next.replaySeconds = normalizeReplaySeconds(changes.replaySeconds);
  if (typeof changes.replayHotkey === 'string') next.replayHotkey = normalizeReplayHotkey(changes.replayHotkey);
  for (const key of ['systemAudio','microphone','autoCheckUpdates']) if (typeof changes[key] === 'boolean') next[key] = changes[key];
  return next;
}
export async function atomicJson(file, value) {
  await mkdir(path.dirname(file), {recursive:true, mode:0o700});
  const temporary = `${file}.${randomUUID()}.tmp`;
  try {
    const handle = await open(temporary, 'wx', 0o600);
    try { await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`); await handle.sync(); } finally { await handle.close(); }
    await rename(temporary, file);
  } finally { await unlink(temporary).catch(() => {}); }
}
export async function loadPreferences(file, defaultFolder) {
  try {
    const value = JSON.parse(await readFile(file, 'utf8'));
    return {settings:normalizeSettings(DEFAULT_SETTINGS, value.settings), outputFolder:validFolder(value.outputFolder) ? value.outputFolder : defaultFolder};
  } catch { return {settings:{...DEFAULT_SETTINGS}, outputFolder:defaultFolder}; }
}
export function validFolder(value) { return typeof value === 'string' && path.isAbsolute(value) && !/[\0\r\n]/.test(value); }
export async function writableFolder(folder, minimum = 0n) {
  if (!validFolder(folder)) throw new Error('Choose an absolute, writable recording folder.');
  await mkdir(folder, {recursive:true});
  if (!(await stat(folder)).isDirectory()) throw new Error('The recording location is not a folder.');
  const probe = path.join(folder, `.camcord-write-test-${randomUUID()}`);
  const handle = await open(probe, 'wx', 0o600);
  await handle.close(); await unlink(probe);
  if (minimum) {
    const space = await statfs(folder, {bigint:true});
    if (space.bavail * space.bsize < minimum) throw new Error('Free at least 512 MB on the recording drive before starting.');
  }
}
export function startupCommand(executable) {
  if (!path.isAbsolute(executable) || /[\0\r\n]/.test(executable)) throw new Error('Invalid startup application path.');
  return `"${executable.replace(/[\\"`$]/g, value => `\\${value}`)}" --startup`;
}
export async function startupEnabled(file, executable) {
  try { return (await readFile(file, 'utf8')).split('\n').includes(`Exec=${startupCommand(executable)}`); } catch { return false; }
}
export async function setStartup(file, executable, enabled) {
  if (!enabled) { await unlink(file).catch(error => {if (error.code !== 'ENOENT') throw error;}); return; }
  await mkdir(path.dirname(file), {recursive:true, mode:0o700});
  await writeFile(file, `[Desktop Entry]\nType=Application\nName=CamCord\nComment=Open CamCord at sign-in without recording\nExec=${startupCommand(executable)}\nTryExec=${executable}\nIcon=camcord\nTerminal=false\nX-GNOME-Autostart-enabled=true\n`, {mode:0o600});
}
