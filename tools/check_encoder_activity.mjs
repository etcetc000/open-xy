import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay} from '../sdk/browser/display.mjs';
import {EncoderActivity} from '../sdk/browser/encoder_activity.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
const module = await WebAssembly.compile(readFileSync(process.argv[2]));
const display = new PreviewDisplay(module), e = display.api;
assert.equal(e.opxy_display_abi(),Number(process.argv[3]));
assert.equal(e.opxy_display_input_bytes(),Number(process.argv[4]));
assert.equal(display.audioWords,Number(process.argv[5]));
const state = new Uint32Array(e.memory.buffer,e.opxy_display_state(),1);
const input = shownControls(Array(8).fill(0),[0,1,2,3]);
const tick = snapshot => display.tick(input,17,0,0,false,null,null,null,snapshot);
tick(null);assert.equal(state[0],0);
for(let mask=0;mask<16;++mask){tick({available:1,mask});assert.equal(state[0],256+mask);}
for(const invalid of [{available:0,mask:1},{available:2,mask:0},{available:1,mask:16},{available:true,mask:0},{available:1,mask:NaN}]) {
 const before = Buffer.from(e.memory.buffer).slice();
 assert.throws(()=>tick(invalid));
 assert.ok(before.equals(Buffer.from(e.memory.buffer)),'Invalid snapshot cannot mutate input/state/frame');
}
const a = new EncoderActivity(), b = new EncoderActivity();
assert.deepEqual(a.snapshot(),{available:1,mask:0});
assert.equal(a.record(1,{type:'input',isTrusted:false}),false);
assert.equal(a.record(1,{type:'change',isTrusted:true}),false);
assert.equal(a.record(1,{type:'keydown',key:'x',isTrusted:true}),false);
assert.equal(a.record(1,{type:'keydown',key:'ArrowUp',isTrusted:true}),true);
a.record(2,{type:'pointerdown',isTrusted:true});
assert.deepEqual(a.snapshot(),{available:1,mask:6});
assert.deepEqual(a.snapshot(),{available:1,mask:6}); // read does not consume
assert.deepEqual(b.snapshot(),{available:1,mask:0}); // per-track isolation
tick(a.snapshot());assert.equal(state[0],262);a.clear();
assert.deepEqual(a.snapshot(),{available:1,mask:0});
assert.throws(()=>a.record(4,{type:'input',isTrusted:true}));
console.log('Encoder ABI, isolation, event validation and atomic snapshot checks passed');
