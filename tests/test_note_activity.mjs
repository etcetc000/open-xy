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
function fixture(abi = 2, inputBytes = abi === 2 ? 76 : 68) {
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
  for (const [abi, bytes] of [[1,76],[2,68],[3,76]]) assert.throws(() => new PreviewDisplay(fixture(abi,bytes)), /ABI/);
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
