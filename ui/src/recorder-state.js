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
  return app.state === 'idle' && app.update.status === 'ready';
}
