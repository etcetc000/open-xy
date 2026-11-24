// The encoders' activity an extended display reads (encoderActivity): one
// bank per visible track and page. Only physical browser gestures enter here; preset
// loads, setParameters and automation never do.
import { physicalGesture } from './display.mjs';

export class EncoderActivity {
  constructor() { this.mask = 0; }
  record(physical, event) {
    if (!Number.isInteger(physical) || physical < 0 || physical > 3) throw new Error('Invalid physical encoder');
    if (!physicalGesture(event)) return false;
    this.mask |= 1 << physical;
    return true;
  }
  snapshot() { return {available: 1, mask: this.mask}; }
  clear() { this.mask = 0; }
}
