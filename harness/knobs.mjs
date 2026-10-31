// Knobs edit their existing range input, sharing its parameter and audio update path.
export function bindKnob(knob, range, onGesture = () => {}) {
  const min = Number(range.min), max = Number(range.max), step = Number(range.step);
  let drag = null;
  function setValue(value, gesture) {
    const clamped = Math.min(max, Math.max(min, value));
    range.value = String(Math.min(max, Math.max(min, min + Math.round((clamped - min) / step) * step)));
    range.dispatchEvent(new Event('input', { bubbles: true }));
    onGesture(gesture);
  }
  function endDrag(event) {
    if (!drag || event?.pointerId !== undefined && event.pointerId !== drag.pointerId) return;
    const { pointerId } = drag;
    drag = null;
    knob.classList.remove('dragging');
    if (knob.hasPointerCapture(pointerId)) knob.releasePointerCapture(pointerId);
  }
  knob.addEventListener('pointerdown', event => {
    if (event.button !== 0 || drag) return;
    event.preventDefault();
    // Pointer editing must not leave a keyboard focus ring behind.
    knob.blur();
    knob.setPointerCapture(event.pointerId);
    drag = { pointerId: event.pointerId, y: event.clientY, value: Number(range.value) };
    knob.classList.add('dragging');
    onGesture(event);
  });
  knob.addEventListener('pointermove', event => {
    if (!drag || event.pointerId !== drag.pointerId) return;
    event.preventDefault();
    const sensitivity = (max - min) / (event.shiftKey ? 2000 : 200);
    drag.value = Math.min(max, Math.max(min, drag.value + (drag.y - event.clientY) * sensitivity));
    drag.y = event.clientY;
    setValue(drag.value, event);
  });
  for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) knob.addEventListener(type, endDrag);
  knob.addEventListener('blur', () => endDrag());
  const doc = knob.ownerDocument;
  doc.defaultView.addEventListener('blur', () => endDrag());
  doc.addEventListener('visibilitychange', () => { if (doc.hidden) endDrag(); });
  knob.addEventListener('keydown', event => {
    const direction = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[event.key];
    let value = Number(range.value);
    if (direction) value += direction * step * (event.shiftKey ? 1 : 10);
    else if (event.key === 'PageUp') value += (max - min) / 10;
    else if (event.key === 'PageDown') value -= (max - min) / 10;
    else if (event.key === 'Home') value = min;
    else if (event.key === 'End') value = max;
    else if (event.key === 'Escape') { endDrag(); return; }
    else return;
    event.preventDefault();
    endDrag();
    setValue(value, event);
  });
}
