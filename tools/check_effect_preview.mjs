// Shared effect ABI/lifecycle smoke check, independent of a particular sound.
import {readFileSync} from 'node:fs';
import {EffectPreviewEngine} from '../sdk/browser/effect.mjs';

function require(value, message) { if (!value) throw new Error(message); }
const module = await WebAssembly.compile(readFileSync(process.argv[2]));
const engine = new EffectPreviewEngine(module, 44100, Array(8).fill(0));
const memory = engine.api.memory.buffer;
let fixed = false; try { engine.api.memory.grow(1); } catch { fixed = true; }
require(fixed, 'Effect memory must not grow');
const left = new Float32Array(257), right = new Float32Array(257);
engine.render(left, right);
require(left.every(n => n === 0) && right.every(n => n === 0), 'Stopped effect must be silent');
engine.noteOn();
for (let corner = 0; corner < 16; corner++) {
  const words = Array.from({length:8}, (_, i) => corner >> (i % 4) & 1 ? 0x7fffffff : 0);
  engine.setParameters(words);
  for (let i = 0; i < 8; i++) {
    engine.render(left, right);
    require(left.every(n => Number.isFinite(n) && Math.abs(n) <= 0.26) &&
            right.every(n => Number.isFinite(n) && Math.abs(n) <= 0.26), 'Finite bounded stereo effect output');
    require(words.every((n, j) => n === engine.parameters[j]), 'Read-only effect controls');
    require(engine.api.memory.buffer === memory, 'Fixed effect memory');
  }
}
engine.setBypass(true); engine.render(left, right);
engine.panic(); engine.render(left, right);
require(left.every(n => n === 0) && right.every(n => n === 0), 'Stop clears effect output');
console.log('PASS: browser effect ABI, stereo buffers, parameters, fixed memory and lifecycle');
