import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewEngine} from '../sdk/browser/engine.mjs';
const module = new WebAssembly.Module(readFileSync(process.argv[2]));
const engine = new PreviewEngine(module, 44100, Array(8).fill(0));
assert.equal(engine.outputChannels, 2);
for (const voice of engine.voices) assert.equal(voice.output.length, 128);
const left = new Float32Array(257), right = new Float32Array(257);
assert.throws(() => engine.render(left), /Stereo/);
assert.throws(() => engine.render(left, right.subarray(1)), /equal/);
engine.render(left, right);
assert.ok(left.every(x => x === 0) && right.every(x => x === 0));
engine.noteOn(60, 1);
// Cross kernel boundaries with irregular host quanta; preserve L/R order and phase.
let frame = 0;
for (const size of [1, 63, 65, 128, 257]) {
  engine.render(left.subarray(0, size), right.subarray(0, size));
  for (let i = 0; i < size; ++i, ++frame) {
    const gain = engine.auditionGain * Math.min(1, (frame + 1) * engine.attackStep);
    assert.ok(Math.abs(left[i] - (1000 + frame) * gain) < 1e-10);
    assert.ok(Math.abs(right[i] - (-3000 - 2 * frame) * gain) < 1e-10);
  }
}
// Direct ABI output has exactly 128 words, with intact adjacent guards.
const address = engine.voices.at(-1).outputAddress + 128 * 4;
const guarded = new Int32Array(engine.api.memory.buffer, address, 130);
guarded.fill(7654321);
const voice = engine.voices[0];
engine.api.opxy_render(voice.state, address + 4, engine.parameterAddress, voice.pitchAddress);
assert.equal(guarded[0], 7654321); assert.equal(guarded[129], 7654321);
assert.ok(guarded.subarray(1, 129).every((x, i) => i % 2 ? x < 0 : x > 0));
engine.panic(); engine.render(left, right);
assert.ok(left.every(x => x === 0) && right.every(x => x === 0));
console.log('PASS: stereo ABI, asymmetric L/R, partial blocks, panic and output canaries.');
