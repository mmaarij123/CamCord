import packageInfo from '../package.json' with { type: 'json' };
import { normalizeBitrate } from './recording-quality.js';

export const INITIAL_STATE = {
  state: 'idle',
  elapsedSeconds: 0,
  settings: { height: 1080, fps: 60, bitrateMbps: 0, systemAudio: true, microphone: false, autoCheckUpdates: true },
  appVersion: packageInfo.version,
  startupEnabled: false,
  update: { status: 'idle', message: 'Check for updates.', version: '', progress: 0 },
  encoder: '',
  outputFolder: 'Videos / CamCord Captures',
  lastOutput: '',
  captureExcluded: false,
  selectingSource: false,
  captureSource: { kind: 'display', id: 'preview-primary', label: 'Primary display', ready: true, width: 1920, height: 1080 },
  sources: [{ kind: 'display', id: 'preview-primary', label: 'Primary display · 1920 × 1080', primary: true }],
};

export const STATE_LABELS = {
  idle: 'Ready to record',
  starting: 'Preparing recorder',
  recording: 'Recording',
  pausing: 'Pausing capture',
  paused: 'Paused',
  resuming: 'Resuming capture',
  saving: 'Saving capture',
};

export function formatElapsed(value) {
  const seconds = Number.isFinite(value) ? Math.max(0, Math.floor(value)) : 0;
  return [Math.floor(seconds / 3600), Math.floor((seconds % 3600) / 60), seconds % 60]
    .map((part) => String(part).padStart(2, '0'))
    .join(':');
}

export function updateSettings(current, changes) {
  const settings = { ...current, ...changes };
  settings.bitrateMbps = normalizeBitrate(settings.bitrateMbps);
  if (settings.height !== 1080 && settings.fps === 120) settings.fps = 60;
  return settings;
}

export function parseStateMessage(data) {
  try {
    const message = typeof data === 'string' ? JSON.parse(data) : data;
    if (!message || message.type !== 'state' || !Object.hasOwn(STATE_LABELS, message.state)) return null;
    const object = (value) => value !== null && typeof value === 'object' && !Array.isArray(value);
    if (message.sources !== undefined && (!Array.isArray(message.sources) || !message.sources.every((source) =>
      object(source) && ['display', 'window'].includes(source.kind) && typeof source.id === 'string' && typeof source.label === 'string'))) return null;
    if (message.captureSource !== undefined && (!object(message.captureSource) ||
      (message.captureSource.kind !== undefined && !['display', 'window', 'region'].includes(message.captureSource.kind)) ||
      (message.captureSource.label !== undefined && typeof message.captureSource.label !== 'string') ||
      (message.captureSource.ready !== undefined && typeof message.captureSource.ready !== 'boolean'))) return null;
    if (message.settings !== undefined && (!object(message.settings) ||
      ['systemAudio', 'microphone', 'autoCheckUpdates'].some((key) => message.settings[key] !== undefined && typeof message.settings[key] !== 'boolean'))) return null;
    if (message.update !== undefined && (!object(message.update) ||
      ['message', 'version', 'status'].some((key) => message.update[key] !== undefined && typeof message.update[key] !== 'string'))) return null;
    if (message.notice !== undefined && (!object(message.notice) || typeof message.notice.text !== 'string' ||
      !['success', 'info', 'warning', 'error'].includes(message.notice.severity))) return null;
    if (['outputFolder', 'lastOutput', 'encoder', 'appVersion'].some((key) => message[key] !== undefined && typeof message[key] !== 'string')) return null;
    return message;
  } catch {
    return null;
  }
}

export function mergeState(previous, message) {
  return {
    ...previous,
    ...message,
    settings: updateSettings(previous.settings, message.settings),
    update: { ...previous.update, ...message.update },
    captureSource: { ...previous.captureSource, ...message.captureSource },
    sources: Array.isArray(message.sources) ? message.sources : previous.sources,
  };
}

export function canInstallUpdate(app) {
  return app.state === 'idle' && !app.selectingSource && app.update.status === 'ready';
}
