import test from 'node:test';
import assert from 'node:assert/strict';
import { bindKnob } from '../harness/knobs.mjs';

function fixture(value = 0.5) {
  const knob = new EventTarget(), range = new EventTarget(), doc = new EventTarget();
  doc.defaultView = new EventTarget();
  knob.ownerDocument = doc;
  const classes = new Set(), captured = new Set();
  knob.classList = { add: value => classes.add(value), remove: value => classes.delete(value) };
  knob.focus = () => { knob.focused = true; };
  knob.blur = () => { knob.focused = false; };
  knob.setPointerCapture = id => captured.add(id);
  knob.hasPointerCapture = id => captured.has(id);
  knob.releasePointerCapture = id => captured.delete(id);
  Object.assign(range, { min: '0', max: '1', step: '0.001', value: String(value) });
  const edits = [];
  range.addEventListener('input', () => edits.push(Number(range.value)));
  bindKnob(knob, range);
  function pointer(type, clientY = 100, extra = {}) {
    knob.dispatchEvent(Object.assign(new Event(type, { cancelable: true }),
      { pointerId: 1, button: 0, clientY, shiftKey: false, ...extra }));
  }
  function key(key, shiftKey = false) {
    knob.dispatchEvent(Object.assign(new Event('keydown', { cancelable: true }), { key, shiftKey }));
  }
  return { knob, range, doc, edits, classes, pointer, key };
}

test('relative dragging uses the existing range input without a jump and releases capture', () => {
  const f = fixture(0.25);
  f.knob.focus();
  f.pointer('pointerdown');
  assert.equal(f.knob.focused, false);
  assert.equal(f.knob.hasPointerCapture(1), true);
  assert.deepEqual(f.edits, []);
  f.pointer('pointermove', 60, { pointerId: 2 });
  assert.deepEqual(f.edits, []);
  f.pointer('pointermove', 60, { clientX: -500 });
  assert.deepEqual(f.edits, [0.45]);
  f.pointer('pointerup', 60);
  f.pointer('pointermove', 0);
  assert.deepEqual(f.edits, [0.45]);
  assert.equal(f.knob.hasPointerCapture(1), false);
  assert.equal(f.classes.has('dragging'), false);
});

test('dragging clamps both endpoints and responds immediately when direction reverses', () => {
  const f = fixture();
  f.pointer('pointerdown');
  f.pointer('pointermove', -200);
  assert.equal(Number(f.range.value), 1);
  f.pointer('pointermove', -195);
  assert.equal(Number(f.range.value), 0.975);
  f.pointer('pointermove', 300);
  assert.equal(Number(f.range.value), 0);
  f.pointer('pointermove', 298);
  assert.equal(Number(f.range.value), 0.01);
});

test('Shift drag is ten times finer and retains small accumulated movements', () => {
  const normal = fixture(), fine = fixture();
  normal.pointer('pointerdown'); fine.pointer('pointerdown');
  normal.pointer('pointermove', 80);
  fine.pointer('pointermove', 80, { shiftKey: true });
  assert.equal(Number(normal.range.value), 0.6);
  assert.equal(Number(fine.range.value), 0.51);
  fine.pointer('pointermove', 79, { shiftKey: true });
  fine.pointer('pointermove', 78, { shiftKey: true });
  assert.equal(Number(fine.range.value), 0.511);
});

test('cancellation, capture loss and focus loss cannot leave a stale drag active', () => {
  for (const stop of ['pointercancel', 'lostpointercapture', 'blur', 'window-blur', 'hidden', 'Escape']) {
    const f = fixture();
    f.pointer('pointerdown');
    if (stop === 'window-blur') f.doc.defaultView.dispatchEvent(new Event('blur'));
    else if (stop === 'hidden') { f.doc.hidden = true; f.doc.dispatchEvent(new Event('visibilitychange')); }
    else if (stop === 'Escape') f.key('Escape');
    else f.pointer(stop);
    f.pointer('pointermove', 0);
    assert.deepEqual(f.edits, [], stop);
    assert.equal(f.knob.hasPointerCapture(1), false, stop);
    f.range.value = '0.2';
    f.pointer('pointerdown'); f.pointer('pointermove', 80);
    assert.ok(Math.abs(Number(f.range.value) - 0.3) < 1e-9, stop);
  }
});

test('keyboard adjustments share the range value and right-click does not start dragging', () => {
  const f = fixture();
  f.pointer('pointerdown', 100, { button: 2 }); f.pointer('pointermove', 0);
  assert.deepEqual(f.edits, []);
  f.key('ArrowUp'); assert.equal(Number(f.range.value), 0.51);
  f.key('ArrowLeft', true); assert.equal(Number(f.range.value), 0.509);
  f.key('PageDown'); assert.ok(Math.abs(Number(f.range.value) - 0.409) < 1e-9);
  f.key('Home'); assert.equal(Number(f.range.value), 0);
  f.key('End'); assert.equal(Number(f.range.value), 1);
  f.key('ArrowRight'); assert.equal(Number(f.range.value), 1);
});
