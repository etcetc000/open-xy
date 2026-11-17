import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createControls, editedShown, pressedShown, shiftWord} from '../sdk/browser/controls.mjs';
import {renderScreen} from '../harness/screen.mjs';

// A synth's Shift row (SDK 1.13) in the browser preview: tests/fixtures/machines/probeshift.
const definition = JSON.parse(readFileSync(new URL('./fixtures/machines/probeshift/definition.json', import.meta.url)));
const plain = JSON.parse(readFileSync(new URL('./fixtures/machines/probesynth/definition.json', import.meta.url)));

test('the row renders as parameters 8..15, stored as the device stores it', () => {
  const controls = createControls(definition);
  const values = controls.defaults();
  assert.deepEqual(Object.keys(values).slice(8), ['noise', 'drift', 'octave', 'lowcut', 'slide', 'ratio', 'balance']);
  const words = controls.q31(values);
  assert.equal(words.length, 16);
  assert.deepEqual(words.slice(8), [0, 536870912, 1073741822, 0, 0, 0, 1073741824, 1073741824]);
  // An amount moves in hundredths; a list by its choices.
  assert.equal(controls.q31({...values, noise: 0.123})[8], shiftWord(definition.shift[0], 0.12));
  assert.equal(controls.displayValue('noise', {...values, noise: 0.123}), '12.0');
  assert.equal(controls.displayValue('octave', {...values, octave: 0.99}), '+2');
});

test('a value may leave the row out, and a preset without it plays its defaults', () => {
  const controls = createControls(definition);
  const page = Object.fromEntries(definition.parameters.map(p => [p.id, p.default]));
  assert.deepEqual(controls.validateParameters(page), controls.defaults());
  assert.throws(() => controls.validateParameters({...page, gap: 0}), /defined normalized controls/);
  assert.throws(() => controls.validateParameters({...page, noise: 2}), /defined normalized controls/);
  const [name, preset] = Object.entries(definition.presets)[0];
  assert.deepEqual(controls.presets[name], {...preset, ...Object.fromEntries(
    definition.shift.filter(p => !p.absent).map(p => [p.id, p.default]))});
});

test('a preset may set the row; the rest of the row plays its defaults', () => {
  const controls = createControls(definition);
  assert.equal(controls.presets.Pulse.noise, 0.3);
  assert.equal(controls.presets.Pulse.drift, 0.25);
  assert.equal(controls.q31(controls.presets.Pulse)[10], shiftWord(definition.shift[2], 0.75));
});

test('a synth without a row keeps eight words and its exact controls', () => {
  const controls = createControls(plain);
  assert.equal(controls.shift.length, 0);
  assert.equal(controls.q31(controls.defaults()).length, 8);
  assert.throws(() => controls.validateParameters({...controls.defaults(), noise: 0}), /defined normalized controls/);
});

// The header's titles, as the workbench draws its screen (harness/screen.mjs).
const titles = (controls, shown, held) =>
  [...renderScreen(controls.defaults(), 'routing', null, '', controls, shown, ['', '', '', ''], held)
    .matchAll(/<text[^>]*>([^<]*)<\/text>/g)].map(m => m[1]);

test('a press with Shift held switches the page, and the row shown follows', () => {
  const controls = createControls(definition);
  const paged = definition.press === 'page';
  const page2 = pressedShown([0, 1, 2, 3], controls.positions, 0, paged);
  assert.deepEqual(page2, [4, 5, 6, 7]);
  // Any encoder, either way, and the same page an edit of a page-2 control shows.
  for (const position of [0, 1, 2, 3]) {
    assert.deepEqual(pressedShown([0, 1, 2, 3], controls.positions, position, paged), page2);
    assert.deepEqual(pressedShown(page2, controls.positions, position, paged), [0, 1, 2, 3]);
  }
  assert.deepEqual(page2, editedShown([0, 1, 2, 3], controls.positions, 4, paged));
  // Held, the header names the page's Shift entries: entries 4..7 on page 2 (5 absent).
  const held = titles(controls, page2, true);
  for (const entry of ['slide', 'ratio', 'balance']) assert.ok(held.includes(entry), entry);
  assert.ok(!held.includes('noise'));
  const first = titles(controls, [0, 1, 2, 3], true);
  for (const entry of ['noise', 'drift', 'octave', 'lowcut']) assert.ok(first.includes(entry), entry);
});

test('a press without Shift switches the same page and the header shows its controls', () => {
  const controls = createControls(definition);
  const page2 = pressedShown([0, 1, 2, 3], controls.positions, 2, true);
  const shown = titles(controls, page2, false);
  for (const p of definition.parameters.slice(4)) assert.ok(shown.includes(p.label.toLowerCase()), p.id);
  assert.ok(!shown.includes('slide'));
});

test('a synth without a row presses as before', () => {
  const controls = createControls(plain);
  // Four controls and no alternates: nothing to move to.
  for (const position of [0, 1, 2, 3]) assert.deepEqual(pressedShown([0, 1, 2, 3], controls.positions, position, false), [0, 1, 2, 3]);
  // Alternates cycle the pressed position alone, back to its own after its last.
  const positions = [0, 1, 2, 3, 0, 1, null, null];
  assert.deepEqual(pressedShown([0, 1, 2, 3], positions, 1, false), [0, 5, 2, 3]);
  assert.deepEqual(pressedShown([0, 5, 2, 3], positions, 1, false), [0, 1, 2, 3]);
  assert.deepEqual(pressedShown([0, 1, 2, 3], positions, 2, false), [0, 1, 2, 3]);
});
