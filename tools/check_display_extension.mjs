import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay, SvgDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const module = new WebAssembly.Module(readFileSync(process.argv[2]));
const display = new PreviewDisplay(module);
assert.equal(display.api.opxy_display_abi(), 7);
assert.equal(display.api.opxy_display_extension(), 0);
const input = shownControls(Array(8).fill(0), [0,1,2,3]);
const frame = display.tick(input, 17, 0, 0, false, null, 0, 0, [0, 0, 0, 0], null, {pan: 0, level: 0}, 0, null);
assert.equal(frame[5], 1, 'The track reaches the exact affine x slot');
assert.equal(display.tick(input, 17, 0, 2, false)[5], 3);
// Exercise the real browser applier without a DOM parser. Bound leaves record
// attributes; retain the authored RGB paint while applying exact Q10 matrices.
const nodes = Array.from({length:2}, () => ({attributes:{stroke:'#12AbEf'},
  setAttribute(name, value) { this.attributes[name] = value; }}));
const renderer = Object.assign(Object.create(SvgDisplay.prototype), {
  points:0, paths:[], nodes, texts:[], keys:[], applied:['',''], shown:[], palette:[], fillable:[false,false],
  extended:true, stride:1, concealed:[], authoredVisibility:[], strokeable:[true,true],
  affine:true, nodesAt:1, nodeWords:8, textsAt:17, headerAt:26, frameWords:34,
});
renderer.apply(display.tick(input, 17, 0, 0, false));
assert.equal(nodes[0].attributes.transform, 'matrix(1.0009765625 0.0029296875 -0.0048828125 0.5 0.0009765625 0)');
assert.equal(nodes[1].attributes.transform, 'matrix(-1 0 0 0 0 1)');
assert.equal(nodes[0].attributes.stroke, '#12AbEf');
console.log('PASS: affine Q10 from the compiled display to the browser applier, with preserved paint.');
