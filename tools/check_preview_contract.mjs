// Firmware-free checks of the shared host contract, independent of a sound's
// reference waveform. Silence and intentional algorithm changes are permitted.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewEngine} from '../sdk/browser/engine.mjs';
import {PreviewDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';

const module = await WebAssembly.compile(readFileSync(process.argv[2]));
const engine = new PreviewEngine(module, 44100, Array(8).fill(0));
const display = new PreviewDisplay(module);
const memory = engine.api.memory.buffer, displayMemory = display.api.memory.buffer;
assert.equal(engine.blockFrames, 64);
assert.equal(engine.parameterCount, 8);
assert.ok(memory.byteLength <= 1024 * 1024, 'Preview memory limit');
assert.throws(() => engine.api.memory.grow(1), RangeError);
assert.throws(() => display.api.memory.grow(1), RangeError);
assert.equal(engine.api.opxy_voice_state(engine.voices.length), 0, 'Factory bound');
const samples = new Float32Array(257);
engine.render(samples);
assert.ok(samples.every(value => value === 0), 'Inactive voices are silent');
const peak = engine.api.opxy_output_peak_q31();
for (let corner = 0; corner < 16; ++corner) {
  // Every slot at both extremes: the four controls, then their alternates inverted.
  const controls = Array.from({length:8}, (_, i) => (corner >> (i & 3) & 1) ^ (i >> 2) ? 0x7fffffff : 0);
  engine.setParameters(controls);
  for (let voice = 0; voice < engine.voices.length + 2; ++voice) engine.noteOn(48 + voice * 2);
  const pitches = engine.voices.map(voice => Array.from(voice.pitch));
  for (let block = 0; block < 8; ++block) {
    engine.render(samples);
    assert.ok(samples.every(value => Number.isFinite(value)), 'Finite audition output');
    assert.deepEqual(Array.from(engine.parameters), controls, 'Read-only controls');
    for (const [i, voice] of engine.voices.entries()) {
      assert.deepEqual(Array.from(voice.pitch), pitches[i], 'Read-only pitch');
      assert.ok(voice.output.every(value => Math.abs(value) <= peak), 'Declared output peak');
    }
    // Each position shows its own control, then its first alternate slot.
    const input = shownControls(controls, block & 1 ? [4, 5, 6, 7] : [0, 1, 2, 3]);
    display.tick(input, 17, corner * 8 + block, 0, false);
    assert.equal(engine.api.memory.buffer, memory);
    assert.equal(display.api.memory.buffer, displayMemory);
  }
  for (const voice of engine.voices) engine.noteOff(voice.note);
  const release = new Float32Array(8192);
  engine.render(release);
  assert.ok(engine.voices.every(voice => !voice.active), 'Release completes');
  assert.ok(release.subarray(6000).every(value => value === 0), 'Release ends in silence');
  engine.noteOn(60); engine.render(samples); engine.panic(); engine.render(samples);
  assert.ok(samples.every(value => value === 0), 'Panic clears voices');
}
console.log('PASS: preview ABI, fixed memory, controls/pitch ownership, peak bounds, lifecycle and drawing smoke checks.');
