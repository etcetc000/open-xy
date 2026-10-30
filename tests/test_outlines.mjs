import test from 'node:test';
import assert from 'node:assert/strict';
import {outlineFaults} from '../sdk/browser/display.mjs';

// One outline of `points` points, as a PreviewDisplay frame lays it out: key, then x, y pairs.
const frame = points => new Int32Array([1, ...points.flat()]);

test('a line traced out and back over itself has no area', () => {
  const out = [[0, 0], [10, 5], [20, 0]], back = [...out].reverse();
  assert.deepEqual(outlineFaults(frame([...out, ...back]), 1, 6), ['outline 0 has no area']);
});

test('an outline that turns straight back at a point is reported', () => {
  const spur = [[0, 0], [10, 0], [10, 10], [20, 10], [10, 10], [0, 10]];
  assert.deepEqual(outlineFaults(frame(spur), 1, 6), ['outline 0 turns straight back']);
});

test('a thin closed ribbon, a figure eight and a collapsed outline pass', () => {
  const ribbon = [[0, -1], [10, 4], [20, -1], [20, 1], [10, 6], [0, 1]];
  assert.deepEqual(outlineFaults(frame(ribbon), 1, 6), []);
  // Its two loops cancel in the signed area, but it encloses both.
  const eight = [[0, 0], [10, 10], [20, 0], [20, 10], [10, 0], [0, 10]];
  assert.deepEqual(outlineFaults(frame(eight), 1, 6), []);
  assert.deepEqual(outlineFaults(frame(Array(6).fill([3, 3])), 1, 6), []);
});
