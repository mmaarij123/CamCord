import test from 'node:test';
import assert from 'node:assert/strict';
import { INITIAL_STATE, formatElapsed, updateSettings, parseStateMessage, mergeState, canInstallUpdate } from '../src/recorder-state.js';

test('elapsed time remains readable for long recordings and invalid inputs', () => {
  assert.equal(formatElapsed(27 * 60), '00:27:00');
  assert.equal(formatElapsed(27 * 3600 + 61), '27:01:01');
  assert.equal(formatElapsed(-12), '00:00:00');
  assert.equal(formatElapsed(Number.NaN), '00:00:00');
  assert.equal(formatElapsed(5.8), '00:00:05');
});

test('update downloads preserve capture preferences and installation waits for idle', () => {
  const previous = { ...INITIAL_STATE, state: 'recording', startupEnabled: true, update: { status: 'downloading', version: '1.5.0', progress: 20 } };
  const next = mergeState(previous, { type: 'state', state: 'recording', update: { progress: 80 } });
  assert.equal(next.startupEnabled, true);
  assert.equal(next.settings.autoCheckUpdates, true);
  assert.equal(next.update.version, '1.5.0');
  assert.equal(next.update.progress, 80);
  assert.equal(canInstallUpdate(next), false);
  for (const state of ['recording', 'paused', 'saving', 'starting', 'pausing', 'resuming']) {
    assert.equal(canInstallUpdate({ ...next, state, update: { status: 'ready' } }), false);
  }
  assert.equal(canInstallUpdate({ ...next, state: 'idle', update: { status: 'ready' } }), true);
  assert.equal(canInstallUpdate({ ...next, state: 'idle', update: { status: 'error' } }), false);
  assert.equal(canInstallUpdate({ ...next, state: 'idle', update: { status: 'ready' } }, true), false);
});

test('switching resolution preserves valid FPS and clamps 120 FPS outside 1080p', () => {
  const current = { ...INITIAL_STATE.settings, fps: 120 };
  assert.equal(updateSettings(current, { height: 720 }).fps, 60);
  assert.equal(updateSettings(current, { height: 480 }).fps, 60);
  assert.equal(updateSettings(current, { height: 1080 }).fps, 120);
  assert.equal(current.fps, 120);
});

test('the bridge ignores malformed or unrelated input instead of crashing the UI', () => {
  for (const data of [null, undefined, '{bad', '{}', 'null', { type: 'state', state: 'unknown' }, { type: 'other', state: 'idle' }]) {
    assert.equal(parseStateMessage(data), null);
  }
  for (const state of ['starting', 'recording', 'pausing', 'paused', 'resuming', 'saving', 'idle']) {
    assert.equal(parseStateMessage(JSON.stringify({ type: 'state', state })).state, state);
  }
});

test('partial native updates preserve recording preferences and folder paths', () => {
  const previous = { ...INITIAL_STATE, outputFolder: 'D:\\Recordings' };
  const next = mergeState(previous, { type: 'state', state: 'saving', elapsedSeconds: 1620, settings: { fps: 30 } });
  assert.equal(next.settings.systemAudio, true);
  assert.equal(next.settings.fps, 30);
  assert.equal(next.outputFolder, 'D:\\Recordings');
  assert.equal(previous.settings.fps, 60);
});

test('malformed nested host state cannot crash source controls or alerts', () => {
  for (const fields of [{ sources: [null] }, { sources: [{}] }, { sources: {} },
    { captureSource: [] }, { captureSource: { ready: 'yes' } }, { settings: { microphone: {} } },
    { update: { message: {} } }, { notice: { text: {}, severity: 'error' } },
    { notice: { text: 'bad', severity: 'invalid' } }, { outputFolder: {} }]) {
    assert.equal(parseStateMessage({ type: 'state', state: 'idle', ...fields }), null);
  }
  assert.ok(parseStateMessage({ type: 'state', state: 'idle', sources: [], captureSource: { kind: 'window', ready: false },
    notice: { text: 'Choose a source', severity: 'warning' } }));
});
