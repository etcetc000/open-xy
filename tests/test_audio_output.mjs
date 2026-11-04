import test from 'node:test';
import assert from 'node:assert/strict';
import { createOutputMute } from '../harness/audio-output.mjs';

function parameter() {
  const calls = [];
  return {
    calls,
    setValueAtTime: (value, time) => calls.push({ type: 'initial', value, time }),
    setTargetAtTime: (value, time, smoothing) => calls.push({ type: 'target', value, time, smoothing }),
  };
}

test('mute can be selected before audio exists and is applied before connecting output', () => {
  const mute = createOutputMute(), gain = parameter();
  assert.equal(mute.muted, false);
  mute.toggle();
  mute.attach(gain, { currentTime: 0, state: 'running' });
  assert.equal(mute.muted, true);
  assert.deepEqual(gain.calls, [{ type: 'initial', value: 0, time: 0 }]);
});

test('mute and unmute change only the output gain while the context keeps running', () => {
  const mute = createOutputMute(), gain = parameter();
  const context = { currentTime: 1, state: 'running', suspend: () => { throw new Error('Mute must not stop playback'); } };
  mute.attach(gain, context);
  context.currentTime = 2;
  assert.equal(mute.toggle(), true);
  context.currentTime = 3;
  assert.equal(mute.toggle(), false);
  assert.equal(context.state, 'running');
  assert.deepEqual(gain.calls, [
    { type: 'initial', value: 1, time: 1 },
    { type: 'target', value: 0, time: 2, smoothing: 0.01 },
    { type: 'target', value: 1, time: 3, smoothing: 0.01 },
  ]);
});

test('mute survives failed audio setup and applies to a replacement output', () => {
  const mute = createOutputMute(), first = parameter(), replacement = parameter();
  mute.attach(first, { currentTime: 0, state: 'running' });
  mute.toggle();
  mute.detach();
  const before = first.calls.length;
  mute.toggle(); mute.toggle();
  assert.equal(first.calls.length, before);
  mute.attach(replacement, { currentTime: 0, state: 'running' });
  assert.equal(replacement.calls[0].value, 0);
});

test('a closed context cannot receive more output automation', () => {
  const mute = createOutputMute(), gain = parameter(), context = { currentTime: 0, state: 'running' };
  mute.attach(gain, context);
  context.state = 'closed';
  mute.toggle();
  assert.equal(mute.muted, true);
  assert.equal(gain.calls.length, 1);
});
