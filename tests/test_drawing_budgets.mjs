import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {drawingFrameBytes, outlineFaults} from '../sdk/browser/display.mjs';
const cases = JSON.parse(readFileSync(new URL('./fixtures/drawing-budgets.json', import.meta.url)));
test('retained drawing budgets match native and metadata cases', () => {
  for (const row of cases) {
    if (row.bytes === null) assert.throws(() => drawingFrameBytes(...row.shape));
    else assert.equal(drawingFrameBytes(...row.shape), row.bytes);
  }
  for (const value of [NaN, Infinity, -1, 1.5])
    assert.throws(() => drawingFrameBytes(value, 4, 128, 80, 1));
});
test('128 independent short rectangles retain valid outlines', () => {
  const frame = new Int32Array(drawingFrameBytes(128, 4, 128, 80, 16) / 4);
  for (let i = 0; i < 128; ++i) frame.set([i + 1, -65536, 0, 65536, 0, 65536, 8192, -65536, 8192], i * 9);
  frame[127 * 9 + 3] = frame[127 * 9 + 5] = 32768;
  assert.equal(frame[3], 65536);
  assert.deepEqual(outlineFaults(frame, 128, 4), []);
});
