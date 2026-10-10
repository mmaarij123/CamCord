import test from 'node:test';
import assert from 'node:assert/strict';
import { INITIAL_STATE, mergeState, parseStateMessage } from '../src/recorder-state.js';

test('source and catalog survive partial timer/settings messages', () => {
  const previous = { ...INITIAL_STATE, captureSource: { kind: 'display', id: 'display:2', label: 'Display 2', ready: true, width: 1920, height: 1080 },
    sources: [{ id: 'display:2', kind: 'display', label: 'Display 2' }] };
  const next = mergeState(previous, { type: 'state', state: 'recording', elapsedSeconds: 5, captureSource: { ready: false } });
  assert.equal(next.captureSource.kind, 'display');
  assert.equal(next.captureSource.width, 1920);
  assert.equal(next.captureSource.ready, false);
  assert.equal(next.sources, previous.sources);
  assert.equal(previous.captureSource.ready, true);
});

test('only Screen and Window capture states are accepted', () => {
  for (const kind of ['display', 'window']) {
    assert.ok(parseStateMessage({ type: 'state', state: 'idle', captureSource: { kind, ready: false } }));
  }
  assert.equal(parseStateMessage({ type: 'state', state: 'idle', captureSource: { kind: 'region', ready: true } }), null);
});

test('source refresh replaces the list and preserves recording preferences', () => {
  const catalog = [{ id: 'window:1', kind: 'window', label: 'Same title' }, { id: 'window:2', kind: 'window', label: 'Same title' }];
  const next = mergeState(INITIAL_STATE, { type: 'state', state: 'idle', sources: catalog, captureSource: { kind: 'window', id: '', ready: false } });
  assert.equal(next.sources.length, 2);
  assert.notEqual(next.sources[0].id, next.sources[1].id);
  assert.equal(next.settings.bitrateMbps, 0);
  assert.equal(next.captureSource.ready, false);
  assert.equal(mergeState(next, { state: 'idle', sources: null }).sources, catalog);
});
