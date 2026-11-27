import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewEngine} from '../sdk/browser/engine.mjs';
import {PreviewDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const module = new WebAssembly.Module(readFileSync(process.argv[2]));
const engine = new PreviewEngine(module, 44100, Array(8).fill(0x40000000));
const display = new PreviewDisplay(module);
// Extension bits 1 and 8: sixteen audio words, then the levels; 152 + 68 + 16 bytes.
assert.equal(display.api.opxy_display_extension(), 9);
assert.equal(display.api.opxy_display_input_bytes(), 236);
assert.equal(display.outputLevels, true); assert.equal(display.levelsAt, 55);
const input = shownControls(Array(8).fill(0), [0,1,2,3]);
const tick = levels => display.tick(input, 17, 0, 0, false, null, 0, 0, [0, 0, 0, 0], null, {pan: 0, level: 0}, 0, null,
                                    null, null, 0, null, levels);
// The display draws the left output level's low bits at x, -1 when none are reported.
assert.equal(tick(null)[5], -1);
assert.deepEqual(Array.from(display.input.subarray(55, 59)), [0, 0, 0, 0]);
assert.equal(tick({outputLeft: 300, outputRight: -2, input: 0x7fffffff})[5], 300);
assert.deepEqual(Array.from(display.input.subarray(55, 59)), [1, 300, 0xfffffffe, 0x7fffffff]);
// Out-of-range levels are refused before the input changes.
assert.throws(() => tick({outputLeft: 0x80000000}), /level outputLeft/);
assert.deepEqual(Array.from(display.input.subarray(55, 59)), [1, 300, 0xfffffffe, 0x7fffffff]);
// The preview stands in its own output peak, taken once: none before any note.
assert.deepEqual(engine.takeLevels(), {outputLeft: 0, outputRight: 0, input: 0});
engine.noteOn(60); engine.render(new Float32Array(256));
const levels = engine.takeLevels();
assert.ok(levels.outputLeft > 0 && levels.outputLeft === levels.outputRight && levels.input === 0);
assert.deepEqual(engine.takeLevels(), {outputLeft: 0, outputRight: 0, input: 0});
console.log('PASS: output levels after the audio words, refused atomically, and the preview output peak.');
