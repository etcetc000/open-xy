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
// An older workbench has no opxy_note: an unprimed onset must still hand the
// hook a valid velocity, the full default, never zero.
const legacy = new WebAssembly.Instance(module).exports;
legacy.opxy_initialize(legacy.opxy_voice_state(0));
legacy.opxy_start(legacy.opxy_voice_state(0));
const scratch = Number(legacy.__heap_base.value);
legacy.opxy_render(legacy.opxy_voice_state(0), scratch, scratch + 256, scratch + 512);
const unprimed = new Int32Array(legacy.memory.buffer, scratch, 1)[0];
assert.equal(unprimed, 127 << 16, `An unprimed onset renders ${127 << 16}, not ${unprimed}`);
console.log('PASS: the previewed note hook renders the primed velocity (' + low + ' against ' + high + ').');
