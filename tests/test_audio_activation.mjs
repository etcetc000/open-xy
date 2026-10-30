import test from 'node:test';
import assert from 'node:assert/strict';
import { createAudioActivation } from '../harness/audio-activation.mjs';

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return { promise, resolve };
}

test('activation starts within the input gesture and plays once startup completes', async () => {
  const ready = deferred(), calls = [];
  const gate = createAudioActivation({ isReady: () => false,
    enableAudio: () => { calls.push('enable'); return ready.promise; } });
  const played = gate.run('key-a', () => calls.push('note'));
  assert.deepEqual(calls, ['enable']);
  assert.equal(gate.isPending('key-a'), true);
  ready.resolve(true);
  assert.equal(await played, true);
  assert.deepEqual(calls, ['enable', 'note']);
  assert.equal(gate.isPending('key-a'), false);
});

test('releasing a key before startup completes prevents a late or stuck note', async () => {
  const ready = deferred(), notes = [];
  const gate = createAudioActivation({ isReady: () => false, enableAudio: () => ready.promise });
  const played = gate.run('pointer-1', () => notes.push(60));
  gate.cancel('pointer-1');
  ready.resolve(true);
  assert.equal(await played, false);
  assert.deepEqual(notes, []);
});

test('stop/blur cancellation clears every waiting key and phrase', async () => {
  const ready = deferred(), plays = [];
  const gate = createAudioActivation({ isReady: () => false, enableAudio: () => ready.promise });
  const requests = ['key-a', 'pointer-1', Symbol('phrase')].map(source => gate.run(source, () => plays.push(source)));
  gate.cancelAll();
  ready.resolve(true);
  assert.deepEqual(await Promise.all(requests), [false, false, false]);
  assert.deepEqual(plays, []);
});

test('a released then re-pressed key plays only the newest request', async () => {
  const ready = deferred(), notes = [];
  const gate = createAudioActivation({ isReady: () => false, enableAudio: () => ready.promise });
  const first = gate.run('key-a', () => notes.push('old'));
  gate.cancel('key-a');
  const second = gate.run('key-a', () => notes.push('new'));
  ready.resolve(true);
  assert.deepEqual(await Promise.all([first, second]), [false, true]);
  assert.deepEqual(notes, ['new']);
});

test('repeated keydown does not replace or duplicate a waiting note', async () => {
  const ready = deferred();
  let starts = 0, plays = 0;
  const gate = createAudioActivation({ isReady: () => false,
    enableAudio: () => { starts++; return ready.promise; } });
  const first = gate.run('key-a', () => plays++);
  assert.equal(await gate.run('key-a', () => plays++), false);
  ready.resolve(true);
  assert.equal(await first, true);
  assert.equal(starts, 1);
  assert.equal(plays, 1);
});

test('ready audio plays immediately without another initialization', async () => {
  let played = false;
  const gate = createAudioActivation({ isReady: () => true, enableAudio: () => { throw new Error('Unexpected activation'); } });
  const result = gate.run('key-a', () => { played = true; });
  assert.equal(played, true);
  assert.equal(await result, true);
});

test('failed startup clears pending state and permits another attempt', async () => {
  for (const reject of [false, true]) {
    let attempts = 0, plays = 0;
    const gate = createAudioActivation({ isReady: () => false, enableAudio: async () => {
      if (++attempts === 1) { if (reject) throw new Error('Audio unavailable'); return false; }
      return true;
    } });
    assert.equal(await gate.run('key-a', () => plays++), false);
    assert.equal(gate.isPending('key-a'), false);
    assert.equal(await gate.run('key-a', () => plays++), true);
    assert.equal(plays, 1);
  }
});
