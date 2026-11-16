import {readFileSync} from 'node:fs';
import {EffectPreviewEngine} from '../../sdk/browser/effect.mjs';
function require(value, message) { if (!value) throw new Error(message); }
const module = await WebAssembly.compile(readFileSync(process.argv[2]));
const words = [0x40000000, 0x60000000, 0, 0x7fffffff, 0, 0, 0, 0];
const fresh = () => new EffectPreviewEngine(module, 44100, words);
const engine = fresh(); engine.setBypass(true); engine.noteOn();
const left = new Float32Array(4096), right = new Float32Array(4096);
engine.render(left, right);
let stereo = false, sounding = false;
for (let i = 300; i < left.length; i++) {
  require(Math.abs(left[i] - engine.phrase[i] / 0x7fffffff * 0.25) < 1e-8, 'Bypass must be the exact dry phrase after fade');
  require(Math.abs(right[i] - engine.phrase[i + 97] / 0x7fffffff * 0.25) < 1e-8, 'Stereo dry phrase');
  stereo ||= left[i] !== right[i]; sounding ||= Math.abs(left[i]) > 0.001;
}
require(stereo && sounding, 'Audible stereo test input');
const elapsed = engine.frames; engine.setBypass(false); engine.setWet(1); engine.render(left, right);
require(engine.frames > elapsed, 'Bypass comparison must preserve the phrase clock');
require(left.some((n, i) => i > 300 && Math.abs(n - engine.phrase[4096 + i] / 0x7fffffff * 0.25) > 0.0001), 'Real effect changes the test phrase');
const whole = fresh(), split = fresh(); whole.noteOn(); split.noteOn();
whole.render(left, right);
const chunks = new Float32Array(4096), chunksR = new Float32Array(4096);
for (let at = 0; at < 4096; at += 113) split.render(chunks.subarray(at, at + 113), chunksR.subarray(at, at + 113));
require(left.every((n, i) => n === chunks[i]) && right.every((n, i) => n === chunksR[i]), 'Block continuity across arbitrary host quanta');
engine.panic(); engine.render(left, right);
require(left.every(n => n === 0) && right.every(n => n === 0), 'Stop silences both channels');
for (const action of [() => engine.setParameters([0]), () => engine.setBypass(1), () => engine.setWet(-1),
  () => new EffectPreviewEngine(module, 48000, words)]) {
  let failed = false; try { action(); } catch { failed = true; } require(failed, 'Invalid audio settings refused');
}
