// Firmware-free checks of a drawing note player's page preview (tooling/preview.py
// compile_player_preview): the drawing transport display.mjs reads, fixed memory, and
// valid frames over a held key, with the player's steps between them.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {PreviewDisplay} from '../sdk/browser/display.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';

const module = await WebAssembly.compile(readFileSync(process.argv[2]));
const display = new PreviewDisplay(module);
const api = display.api;
for (const name of ['opxy_player_press', 'opxy_player_release', 'opxy_player_step'])
  assert.equal(typeof api[name], 'function', name);
assert.equal(api.memory.buffer.byteLength, 2 * 1024 * 1024, 'Fixed preview memory allowance');
assert.throws(() => api.memory.grow(1), RangeError);
const words = Array(8).fill(0), encoders = display.encodersAt ? {available: 1, mask: 0} : null;
const tick = (frame, serial) => display.tick(shownControls(words, [0, 1, 2, 3]), 17, serial, 0, false, null, 0, 0,
  [0, 0, 0, 0], null, {pan: 0, level: 0}, 0, null, encoders && {available: 1, mask: frame % 30 < 5 ? 1 : 0});
// tick throws on an invalid frame. A key is pressed at frame 30 and let go at 600, with the
// player's steps between frames, at each control's extremes.
for (const extreme of [0, 0x7fffffff]) {
  words.fill(extreme);
  display.reset();
  for (let frame = 0, serial = 0; frame < 720; ++frame) {
    if (frame === 30) { api.opxy_player_press(60, 100); ++serial; }
    if (frame === 600) api.opxy_player_release(60);
    for (let step = 0; step < 8; ++step) api.opxy_player_step();
    tick(frame, serial);
  }
}
console.log('PASS: player page preview: drawing transport, fixed memory and valid frames over a held key.');
