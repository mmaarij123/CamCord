import test from 'node:test';
import assert from 'node:assert/strict';
import { BITRATE_PRESETS, normalizeBitrate, videoBitrateKbps, videoPeakMbps, suggestedBitrate, storageEstimate, recordingWidth, frameRatesForHeight } from '../src/recording-quality.js';
import { INITIAL_STATE, updateSettings, mergeState } from '../src/recorder-state.js';

test('all bitrate presets are valid and malformed/out-of-range values become Auto', () => {
  for (const rate of BITRATE_PRESETS) assert.equal(normalizeBitrate(rate), rate);
  for (const rate of [-1, 1, 3, 101, 1000, 16.5, '100', null, undefined, NaN, Infinity]) {
    assert.equal(normalizeBitrate(rate), 0);
  }
  assert.equal(BITRATE_PRESETS.at(-1), 100);
});

test('Auto retains existing resolution/FPS targets and suggests a nearby manual preset', () => {
  for (const [height, fps, kbps] of [[480,15,2500],[480,30,3500],[480,60,5000],
    [720,15,4000],[720,30,5500],[720,60,8000],[1080,15,6000],[1080,30,9000],[1080,60,14000],[1080,120,24000],
    [1440,15,12000],[1440,30,20000],[1440,60,32000],[2160,15,24000],[2160,30,40000],[2160,60,64000]]) {
    const settings = { height, fps, bitrateMbps: 0 };
    assert.equal(videoBitrateKbps(settings), kbps);
    assert.ok(suggestedBitrate(settings) >= kbps / 1000);
  }
});

test('2K/4K preserve 15/30/60 FPS on selection and restore; unsupported rates fall back to 60', () => {
  for (const [height, width] of [[1440,2560],[2160,3840]]) {
    assert.equal(recordingWidth(height), width);
    assert.deepEqual(frameRatesForHeight(height), [15,30,60]);
    for (const fps of [15,30,60,120,999,'30',null]) {
      const expectedFps = [15,30,60].includes(fps) ? fps : 60;
      const settings = updateSettings({...INITIAL_STATE.settings, fps:120, bitrateMbps:100}, {height,fps});
      assert.equal(settings.height, height); assert.equal(settings.fps, expectedFps); assert.equal(settings.bitrateMbps,100);
      const restored = mergeState(INITIAL_STATE, {state:'idle',settings:{height,fps}});
      assert.equal(restored.settings.height,height); assert.equal(restored.settings.fps,expectedFps);
    }
    const storage = storageEstimate({...INITIAL_STATE.settings,height,bitrateMbps:0});
    assert.ok(storage.mbPerMinute > 240);
  }
});

test('resolution changes preserve supported frame rates and normalize 1080p120', () => {
  for (const height of [480,720,1080,1440,2160]) {
    for (const fps of [15,30,60]) assert.equal(updateSettings({...INITIAL_STATE.settings,fps},{height}).fps,fps);
    assert.equal(updateSettings({...INITIAL_STATE.settings,fps:120},{height}).fps,height === 1080 ? 120 : 60);
  }
});

test('manual bitrate survives resolution/FPS changes and peak never exceeds 100 Mbps', () => {
  for (const bitrateMbps of BITRATE_PRESETS.filter(Boolean)) {
    const settings = updateSettings(INITIAL_STATE.settings, { bitrateMbps });
    assert.equal(videoBitrateKbps(settings), bitrateMbps * 1000);
    assert.ok(videoPeakMbps(settings) <= 100);
    assert.ok(videoPeakMbps(settings) >= bitrateMbps);
    assert.equal(updateSettings(settings, { height: 480, fps: 30 }).bitrateMbps, bitrateMbps);
    assert.equal(mergeState({ ...INITIAL_STATE, settings }, { state: 'saving', settings: { fps: 15 } }).settings.bitrateMbps, bitrateMbps);
  }
  assert.equal(videoPeakMbps({ ...INITIAL_STATE.settings, bitrateMbps: 100 }), 100);
  assert.equal(videoPeakMbps({ ...INITIAL_STATE.settings, bitrateMbps: 75 }), 97.5);
  assert.equal(updateSettings(INITIAL_STATE.settings, { bitrateMbps: 999 }).bitrateMbps, 0);
});

test('storage estimates use decimal units and count one mixed AAC track, not two', () => {
  const settings = { ...INITIAL_STATE.settings, bitrateMbps: 100, systemAudio: false, microphone: false };
  assert.deepEqual(storageEstimate(settings), { mbPerMinute: 750, gbPerHour: 45 });
  const audio = storageEstimate({ ...settings, systemAudio: true });
  assert.ok(Math.abs(audio.mbPerMinute - 751.44) < 0.00001);
  assert.deepEqual(storageEstimate({ ...settings, systemAudio: true, microphone: true }), audio);
  assert.deepEqual(storageEstimate({ ...settings, microphone: true }), audio);
});
