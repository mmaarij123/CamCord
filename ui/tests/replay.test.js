import test from 'node:test';
import assert from 'node:assert/strict';
import { REPLAY_DURATIONS, REPLAY_HOTKEYS, normalizeReplaySeconds, normalizeReplayHotkey } from '../src/replay-settings.js';
import { INITIAL_STATE, updateSettings, parseStateMessage, mergeState, canInstallUpdate, canToggleReplay } from '../src/recorder-state.js';
test('replay duration and shortcut recommendations are bounded and persist through partial states',()=>{
  assert.equal(REPLAY_DURATIONS[0],30);assert.equal(REPLAY_DURATIONS.at(-1),300);
  for(const replaySeconds of REPLAY_DURATIONS)assert.equal(updateSettings(INITIAL_STATE.settings,{replaySeconds}).replaySeconds,replaySeconds);
  for(const replayHotkey of REPLAY_HOTKEYS)assert.equal(normalizeReplayHotkey(replayHotkey),replayHotkey);
  for(const value of [-1,0,29,301,100000,'300',null])assert.equal(normalizeReplaySeconds(value),60);
  assert.equal(normalizeReplayHotkey('Alt+F4'),'Ctrl+Shift+F8');
  const state=mergeState(INITIAL_STATE,{state:'idle',settings:{replaySeconds:300,replayHotkey:'Ctrl+Alt+F10'},replay:{enabled:true,bufferedSeconds:42}});
  const next=mergeState(state,{state:'recording',elapsedSeconds:2});assert.equal(next.replay.enabled,true);assert.equal(next.replay.bufferedSeconds,42);assert.equal(next.settings.replaySeconds,300);
});
test('an active replay can be turned off with a lost source or while normal recording saves',()=>{
  const lost={...INITIAL_STATE,captureSource:{ready:false}};
  assert.equal(canToggleReplay(lost),false);
  const active={...lost,replay:{...INITIAL_STATE.replay,enabled:true}};
  assert.equal(canToggleReplay(active),true);
  assert.equal(canToggleReplay({...active,state:'saving'}),true);
  assert.equal(canToggleReplay({...active,replay:{...active.replay,busy:true}}),false);
  assert.equal(canToggleReplay(active,false),false);
  assert.equal(canToggleReplay({...active,update:{status:'installing'}}),false);
});
test('malformed replay states are rejected; updating requires a stopped replay buffer',()=>{
  for(const replay of [null,[],{enabled:1},{busy:'yes'},{saving:{}},{bufferedSeconds:301},{bufferedSeconds:-1},{message:{}},{lastClip:[]}])assert.equal(parseStateMessage({type:'state',state:'idle',replay}),null);
  const ready={...INITIAL_STATE,update:{status:'ready'}};assert.ok(canInstallUpdate(ready));
  for(const replay of [{enabled:true},{busy:true},{enabled:true,saving:true}])assert.equal(canInstallUpdate({...ready,replay}),false);
});
