import test from 'node:test';
import assert from 'node:assert/strict';
import { NoteActivity, PreviewDisplay } from '../sdk/browser/display.mjs';
import { editedShown, shownControls } from '../sdk/browser/controls.mjs';

// Engine-independent WASM storage exercises the actual browser ABI transport.
function unsigned(value) {
  const bytes = [];
  do { let byte = value & 127; value >>>= 7; if (value) byte |= 128; bytes.push(byte); } while (value);
  return bytes;
}
function signed(value) {
  const bytes = [];
  for (;;) {
    let byte = value & 127; value >>= 7;
    const done = value === 0 && !(byte & 64) || value === -1 && (byte & 64);
    if (!done) byte |= 128;
    bytes.push(byte); if (done) return bytes;
  }
}
const vector = items => [...unsigned(items.length), ...items.flat()];
const section = (id, bytes) => [id, ...unsigned(bytes.length), ...bytes];
const name = value => [...unsigned(value.length), ...new TextEncoder().encode(value)];
function fixture(abi = 2, inputBytes = {1:68, 2:76, 3:84, 4:88, 5:104, 6:112}[abi]) {
  const constants = {opxy_display_abi:abi, opxy_display_state_bytes:16,
    opxy_display_input_bytes:inputBytes, opxy_display_frame_bytes:100,
    opxy_display_state:1024, opxy_display_input:2048, opxy_display_frame:4096, opxy_display_valid:1};
  const functions = Object.entries(constants).map(([name, value]) => ({name, type:0, body:[0x41, ...signed(value)]}));
  functions.push({name:'opxy_display_reset', type:1, body:[]}, {name:'opxy_display_render', type:1, body:[]});
  const types = vector([[0x60,0,1,0x7f], [0x60,0,0]]);
  const exports = vector([[...name('memory'),2,0], ...functions.map((f,i) => [...name(f.name),0,...unsigned(i)])]);
  const bodies = vector(functions.map(f => { const bytes = [0,...f.body,0x0b]; return [...unsigned(bytes.length),...bytes]; }));
  return new WebAssembly.Module(new Uint8Array([0,97,115,109,1,0,0,0,
    ...section(1,types), ...section(3,vector(functions.map(f => [f.type]))),
    ...section(5,[1,1,1,1]), ...section(7,exports), ...section(10,bodies)]));
}
const input = () => shownControls(Array(8).fill(0), [0,1,2,3]);

test('note activity counts distinct gates, retriggers, polyphonic releases and panic', () => {
  const notes = new NoteActivity();
  assert.deepEqual([notes.held_notes, notes.note_on_serial, notes.note_off_serial], [0,0,0]);
  notes.noteOn(60); notes.noteOn(64); notes.noteOn(60);
  assert.deepEqual([notes.held_notes, notes.note_on_serial], [2,3]);
  assert.equal(notes.noteOff(60), true);
  assert.deepEqual([notes.held_notes, notes.note_off_serial], [1,1]);
  assert.equal(notes.noteOff(60), false);
  assert.equal(notes.note_off_serial, 1);
  notes.panic(); notes.panic();
  assert.deepEqual([notes.held_notes, notes.note_off_serial], [0,2]);
  for (const value of [-1,128,1.5,NaN,'60',null]) assert.equal(notes.noteOn(value), false);
  assert.equal(notes.noteOn(0), true); assert.equal(notes.noteOn(127), true);
});

test('note counters wrap and independent tracks retain separate gates', () => {
  const first = new NoteActivity(), second = new NoteActivity();
  first.note_on_serial = first.note_off_serial = 0xffffffff;
  first.noteOn(60); first.noteOff(60);
  assert.deepEqual([first.note_on_serial, first.note_off_serial], [0,0]);
  second.noteOn(60); first.panic();
  assert.equal(second.held_notes, 1);
});

test('ABI 2 publishes held notes and releases independently of onset and reduced motion', () => {
  const display = new PreviewDisplay(fixture()), notes = new NoteActivity();
  const memory = display.memory;
  for (const note of [60,64,67]) notes.noteOn(note);
  display.tick(input(),17,3,2,true,notes);
  assert.deepEqual([...display.input.slice(17)], [3,0]);
  notes.noteOff(64);
  display.tick(input(),17,3,2,true,notes);
  assert.deepEqual([...display.input.slice(17)], [2,1]);
  assert.equal(display.input[6], 3, 'Releasing never fabricates a start');
  notes.panic(); display.tick(input(),0,3,2,true,notes);
  assert.deepEqual([...display.input.slice(17)], [0,3]);
  assert.equal(display.api.memory.buffer, memory);
  display.tick(input(),17,3);
  assert.deepEqual([...display.input.slice(17)], [0xffffffff,0], 'Omission invalidates stale activity');
});

test('invalid activity fails before mutating display input', () => {
  const display = new PreviewDisplay(fixture());
  display.tick(input(),17,1,0,false,{held_notes:1,note_off_serial:0});
  const before = new Uint8Array(display.memory).slice();
  for (const activity of [{}, {held_notes:-1,note_off_serial:0}, {held_notes:129,note_off_serial:0},
    {held_notes:1.5,note_off_serial:0}, {held_notes:1,note_off_serial:-1},
    {held_notes:1,note_off_serial:0x100000000}, {held_notes:1,note_off_serial:NaN}]) {
    assert.throws(() => display.tick(input(),99,2,0,false,activity));
    assert.deepEqual(new Uint8Array(display.memory), before);
  }
});

test('ABI 1 previews retain their original storage and unsupported layouts fail', () => {
  const display = new PreviewDisplay(fixture(1));
  const after = new Uint32Array(display.memory,2048+68,1); after[0] = 0xabcdef;
  display.tick(input(),17,2,0,false,{held_notes:2,note_off_serial:9});
  assert.equal(display.input.length, 17); assert.equal(after[0], 0xabcdef);
  for (const [abi, bytes] of [[1,76],[2,68],[3,76],[2,92],[4,92],[5,88]]) assert.throws(() => new PreviewDisplay(fixture(abi,bytes)), /ABI/);
});

test('ABI 5 publishes the amp envelope after the drum model, and omission marks it unknown', () => {
  const display = new PreviewDisplay(fixture(5)), notes = new NoteActivity();
  const amp = {attack:0x00800000, decay:0x20000000, sustain:0x7fffffff, release:0};
  notes.noteOn(60);
  display.tick(input(),17,1,0,false,notes,0,0,[0,0,0,0],amp);
  assert.deepEqual([...display.input.slice(17)], [1,0,0,0,0,0x00800000,0x20000000,0x7fffffff,0]);
  display.tick(input(),17,1,0,false,notes);
  assert.deepEqual([...display.input.slice(22)], Array(4).fill(0xffffffff), 'Omission invalidates a stale envelope');
  const before = new Uint8Array(display.memory).slice();
  for (const bad of [{}, {...amp, attack:-1}, {...amp, sustain:0x80000000}, {...amp, release:0.5}, {...amp, decay:'1'}]) {
    assert.throws(() => display.tick(input(),99,2,0,false,notes,0,0,[0,0,0,0],bad), /amp/);
    assert.deepEqual(new Uint8Array(display.memory), before);
  }
});

test('ABI 4 previews ignore a supplied envelope and keep their storage', () => {
  const display = new PreviewDisplay(fixture(4));
  const after = new Uint32Array(display.memory,2048+88,1); after[0] = 0xabcdef;
  display.tick(input(),17,1,0,false,null,0,0,[0,0,0,0],{attack:1,decay:2,sustain:3,release:4});
  assert.equal(display.input.length, 22); assert.equal(after[0], 0xabcdef);
});

test('an edit shows its parameter at its position, or its whole page on a paged synth', () => {
  // Macro Osc's shape: page 2 holds FM Depth on position 3, then Bits, Rate and Drift on 0..2.
  const positions = [0, 1, 2, 3, 3, 0, 1, 2];
  assert.deepEqual(editedShown([0, 1, 2, 3], positions, 4), [0, 1, 2, 4]);
  assert.deepEqual(editedShown([0, 1, 2, 3], positions, 6, true), [5, 6, 7, 4]);
  assert.deepEqual(editedShown([5, 6, 7, 4], positions, 1, true), [0, 1, 2, 3]);
});

test('partial second pages repeat the same saved controls without switching pages on a turn', () => {
  // Six controls like Solar: the second and fourth columns are shared.
  const solar = [0, 1, 2, 3, 0, 2];
  assert.deepEqual(editedShown([0, 1, 2, 3], solar, 5, true), [4, 1, 5, 3]);
  assert.deepEqual(editedShown([4, 1, 5, 3], solar, 1, true), [4, 1, 5, 3]);
  assert.deepEqual(editedShown([4, 1, 5, 3], solar, 3, true), [4, 1, 5, 3]);
  assert.deepEqual(editedShown([4, 1, 5, 3], solar, 2, true), [0, 1, 2, 3]);
  // Seven controls like Drone: even the first column can be shared.
  const drone = [0, 1, 2, 3, 1, 2, 3];
  assert.deepEqual(editedShown([0, 1, 2, 3], drone, 4, true), [0, 4, 5, 6]);
  assert.deepEqual(editedShown([0, 4, 5, 6], drone, 0, true), [0, 4, 5, 6]);
  assert.deepEqual(editedShown([0, 4, 5, 6], drone, 3, true), [0, 1, 2, 3]);
});

test('ABI 3 publishes Shift and its choices, and ABI 4 a drum kit\'s model after them', () => {
  const shifted = new PreviewDisplay(fixture(3, 84));
  shifted.tick(input(), 17, 3, 2, false, null, 0, 1, [2, 5, 1, 0]);
  assert.deepEqual([...shifted.input.slice(19)], [1, 2 | 5 << 8 | 1 << 16]);
  const kit = new PreviewDisplay(fixture(4, 88));
  kit.tick(input(), 17, 3, 2, false, null, 30, 1, [3, 0, 2, 0]);
  assert.deepEqual([...kit.input.slice(19)], [1, 3 | 2 << 16, 30]);
  assert.throws(() => kit.tick(input(), 17, 3, 2, false, null, 0, 2), /Invalid shift/);
  assert.throws(() => kit.tick(input(), 17, 3, 2, false, null, 0, 0, [256, 0, 0, 0]), /Invalid shift choice/);
  assert.throws(() => new PreviewDisplay(fixture(4, 80)), /Unsupported drawing ABI/);
});

test('ABI 6 publishes a padMix kit key\'s pan, level, pad fields and the encoders\' row after the amp envelope', () => {
  const display = new PreviewDisplay(fixture(6));
  const amp = {attack: 1, decay: 2, sustain: 3, release: 4};
  display.tick(input(), 17, 1, 0, false, null, 3, 1, [1, 2, 3, 0], amp, {pan: -40, level: -6, pad: [-9, 7, 5, 0]}, 1);
  assert.equal(display.input.length, 28);
  assert.equal(display.input[21], 3);
  assert.deepEqual([...display.input.slice(22, 26)], [1, 2, 3, 4], 'The envelope keeps its place');
  const bytes = new Int8Array(display.memory, 2048 + 104, 8);
  assert.deepEqual([...bytes], [-40, -6, 1, 0, -9, 7, 5, 0]);
  display.tick(input(), 17, 1, 0, false, null);
  assert.deepEqual([...bytes], [0, 0, 0, 0, 0, 0, 0, 0], 'Omission is a centred key at 0 dB, neutral, on the first row');
  const before = new Uint8Array(display.memory).slice();
  for (const [mix, line] of [[{pan: 101, level: 0}, 0], [{pan: 0, level: 1}, 0], [{pan: 0, level: -31}, 0],
    [{pan: 0.5, level: 0}, 0], [null, 0], [{pan: 0, level: 0}, 2], [{pan: 0, level: 0, pad: [128]}, 0],
    [{pan: 0, level: 0, pad: [0, 0, 0, 0, 0]}, 0]]) {
    assert.throws(() => display.tick(input(), 17, 1, 0, false, null, 0, 0, [0, 0, 0, 0], null, mix, line));
    assert.deepEqual(new Uint8Array(display.memory), before);
  }
  assert.throws(() => new PreviewDisplay(fixture(6, 104)), /ABI/);
  // An ABI 5 preview keeps its 104 bytes and never receives a key's mix.
  const five = new PreviewDisplay(fixture(5));
  five.tick(input(), 17, 1, 0, false, null, 0, 0, [0, 0, 0, 0], null, {pan: 50, level: -3}, 1);
  assert.equal(five.input.length, 26);
});
