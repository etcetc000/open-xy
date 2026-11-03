// The preview's velocity hook: a machine's note hook renders the note's primed
// velocity, as the device adapter does between initialize and start.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewEngine} from '../sdk/browser/engine.mjs';

const module = await WebAssembly.compile(readFileSync(process.argv[2]));
// A fresh engine per velocity, as the workbench gives each audition one; the
// runner quantizes a 0..1 velocity to stock's 1..127 raw units.
const parameters = new WebAssembly.Instance(module).exports.opxy_parameter_count();
const audition = velocity => {
    const engine = new PreviewEngine(module, 44100, Array(parameters).fill(0));
    assert.ok(engine.noteOn(60, velocity), 'Audition voice');
    engine.render(new Float32Array(engine.blockFrames));
    // The kernel's block, read as the preview contract check does.
    return engine.voices[0].output[0];
};
const quantized = velocity => Math.max(1, Math.round(velocity * 127)) << 16;
const low = audition(0.25), high = audition(1.0);
assert.equal(low, quantized(0.25), `The 0.25 velocity renders ${quantized(0.25)}, not ${low}`);
assert.equal(high, quantized(1.0), `The full velocity renders ${quantized(1.0)}, not ${high}`);
assert.ok(low < high, 'The lower velocity renders the lower level');
assert.ok(Math.abs(low - high * 32 / 127) <= 1, 'The levels hold the quantized 32:127 ratio');
console.log('PASS: the previewed note hook renders the primed velocity (' + low + ' against ' + high + ').');
