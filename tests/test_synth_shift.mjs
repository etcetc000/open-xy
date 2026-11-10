import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createControls, shiftWord} from '../sdk/browser/controls.mjs';

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
