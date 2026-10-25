import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, access } from 'node:fs/promises';
import { INITIAL_STATE } from '../src/recorder-state.js';

test('clipping UI, capture backends and global shortcut registration have been removed',async()=>{
  assert.equal(INITIAL_STATE.replay,undefined);
  assert.equal(INITIAL_STATE.settings.replaySeconds,undefined);
  assert.equal(INITIAL_STATE.settings.replayHotkey,undefined);
  for(const file of ['ui/src/ReplayControl.jsx','ui/src/replay-settings.js','src/ReplayManager.cpp','src/ReplayManager.h','src/ReplaySettings.h','linux/src/replay.mjs']) {
    await assert.rejects(access(new URL(`../../${file}`,import.meta.url)));
  }
  for(const file of ['ui/src/App.jsx','ui/src/recorder-state.js','src/MainWindow.cpp','src/MainWindow.h','src/CaptureEngine.cpp','src/AudioCaptureEngine.cpp','src/SettingsManager.cpp','src/RecordingTypes.h','linux/src/main.mjs','linux/src/recorder.mjs','linux/src/preferences.mjs']) {
    const source=await readFile(new URL(`../../${file}`,import.meta.url),'utf8');
    // The smoke-test assertion itself can mention removed labels; it cannot register a hotkey or start a buffer.
    assert.doesNotMatch(source,/ReplayManager|ReplayControl|RegisterHotKey|globalShortcut|replayEnabled|saveClip|replaySeconds|replayHotkey|this\.rolling|bool rolling/);
  }
});
