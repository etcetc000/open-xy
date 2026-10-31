import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewEngine} from '../sdk/browser/engine.mjs';
import {PreviewDisplay, SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const module = new WebAssembly.Module(readFileSync(process.argv[2]));
const engine = new PreviewEngine(module, 44100, Array(8).fill(0));
const display = new PreviewDisplay(module);
assert.equal(engine.audioWords, 1); assert.equal(display.audioWords, 1);
engine.noteOn(60); engine.render(new Float32Array(64));
const snapshot = engine.takeAudio();
assert.deepEqual(snapshot, {count:1, words:[0x01000001, ...Array(15).fill(0)]});
assert.deepEqual(engine.takeAudio().words, Array(16).fill(0));
engine.api.opxy_audio_track(1); engine.render(new Float32Array(64));
assert.equal(engine.takeAudio(0).words[0], 0);
assert.equal(engine.takeAudio(1).words[0], 0x01000001);
const input = shownControls(Array(8).fill(0), [0,1,2,3]);
const frame = display.tick(input, 17, 0, 0, false, null, null, snapshot);
assert.equal(frame[5], 1, 'Audio snapshot reaches exact affine x slot');
assert.throws(() => display.tick(input, 17, 0, 0, false, null, null, {count:2, words:Array(16).fill(0)}), /telemetry/);
// Exercise the real browser applier without a DOM parser. Bound leaves record
// attributes; retain the authored RGB paint while applying exact Q10 matrices.
const nodes = Array.from({length:2}, () => ({attributes:{stroke:'#12AbEf'},
  setAttribute(name, value) { this.attributes[name] = value; }}));
const renderer = Object.assign(Object.create(SvgDisplay.prototype), {
  points:0, paths:[], nodes, texts:[], keys:[], applied:['',''], shown:[], palette:[], fillable:[false,false],
  affine:true, nodesAt:1, nodeWords:8, textsAt:17, headerAt:26, frameWords:34,
});
renderer.apply(frame);
assert.equal(nodes[0].attributes.transform, 'matrix(1.0009765625 0.0029296875 -0.0048828125 0.5 0.0009765625 0)');
assert.equal(nodes[1].attributes.transform, 'matrix(-1 0 0 0 0 1)');
assert.equal(nodes[0].attributes.stroke, '#12AbEf');
console.log('PASS: affine Q10, preserved paint, per-track telemetry consume and actual audio-to-display handoff.');
