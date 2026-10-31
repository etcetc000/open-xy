// One bank per visible track/page. Only actual browser gestures enter here;
// patch loading, setParameters and automation do not generate physical events.
export class EncoderActivity {
  constructor() { this.mask = 0; }
  record(physical, event) {
    if (!Number.isInteger(physical) || physical < 0 || physical > 3) throw new Error('Invalid physical encoder');
    if (!event?.isTrusted) return false;
    if (event.type !== 'input' && event.type !== 'pointerdown' && event.type !== 'pointermove' &&
        !(event.type === 'keydown' && ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End', 'PageUp', 'PageDown'].includes(event.key))) return false;
    this.mask |= 1 << physical;
    return true;
  }
  snapshot() { return {available: 1, mask: this.mask}; }
  clear() { this.mask = 0; }
}
