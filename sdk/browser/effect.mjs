// Computer-only stereo test phrase through the authored wet-return callback.
// The stock send mixer is not emulated. Bypass keeps DSP and phrase time running.
export class EffectPreviewEngine {
  constructor(module, sampleRate, parameters) {
    if (!(module instanceof WebAssembly.Module) || WebAssembly.Module.imports(module).length)
      throw new Error('A self-contained effect module is required');
    if (sampleRate !== 44100) throw new Error('Effect audition requires 44100 Hz');
    this.api = new WebAssembly.Instance(module).exports;
    if (this.api.opxy_effect_abi() !== 1) throw new Error('Unsupported effect audition ABI');
    const memory = this.api.memory.buffer;
    if (memory.byteLength !== 2 * 1024 * 1024) throw new Error('Invalid effect memory budget');
    const view = (name, length) => {
      const address = this.api[name]();
      if (!Number.isInteger(address) || address < 4 || address % 4 || address + length * 4 > memory.byteLength)
        throw new Error('Invalid effect buffer');
      return new Int32Array(memory, address, length);
    };
    this.left = view('opxy_effect_left', 64); this.right = view('opxy_effect_right', 64);
    this.parameters = view('opxy_effect_parameters', 8);
    const regions = [this.left, this.right, this.parameters];
    for (let i = 0; i < regions.length; i++) for (let j = 0; j < i; j++) {
      if (regions[i].byteOffset < regions[j].byteOffset + regions[j].byteLength &&
          regions[j].byteOffset < regions[i].byteOffset + regions[i].byteLength) throw new Error('Overlapping effect buffers');
    }
    this.dryLeft = new Int32Array(64); this.dryRight = new Int32Array(64);
    this.blockFrames = 64; this.cursor = 64; this.frames = 0;
    this.playing = false; this.bypass = false; this.wet = 0.5; this.blend = 0.5;
    // A two-second C/E/G/C pluck phrase, with a brief silence after each note
    // to expose echoes and tails. Precompute outside the audio callback.
    this.phrase = new Int32Array(88200);
    const notes = [60, 64, 67, 72];
    for (let i = 0; i < this.phrase.length; i++) {
      const age = i % 22050, t = age / sampleRate;
      const hz = 440 * 2 ** ((notes[Math.floor(i / 22050)] - 69) / 12);
      const envelope = Math.min(1, age / 128) * Math.exp(-t * 18);
      this.phrase[i] = Math.round((Math.sin(2 * Math.PI * hz * t) + 0.35 * Math.sin(4 * Math.PI * hz * t)) * envelope * 0x18000000);
    }
    this.setParameters(parameters); this.api.opxy_effect_reset();
  }
  setParameters(values) {
    if (!Array.isArray(values) || values.length !== 8 || values.some(n => !Number.isInteger(n) || n < 0 || n > 0x7fffffff))
      throw new Error('Expected eight nonnegative Q31 parameter words');
    this.parameters.set(values);
  }
  setModel() {}
  setBypass(value) { if (typeof value !== 'boolean') throw new Error('Invalid bypass'); this.bypass = value; }
  setWet(value) {
    if (!Number.isFinite(value) || value < 0 || value > 1) throw new Error('Invalid effect mix');
    this.wet = value;
  }
  noteOn() { this.playing = true; return true; }
  noteOff() { this.panic(); }
  panic() {
    this.playing = false; this.cursor = 64; this.frames = 0;
    this.api.opxy_effect_reset();
  }
  render(output, right = output) {
    if (!this.playing) { output.fill(0); if (right !== output) right.fill(0); return; }
    for (let i = 0; i < output.length; i++) {
      if (this.cursor === 64) {
        for (let j = 0; j < 64; j++) {
          this.left[j] = this.dryLeft[j] = this.phrase[(this.frames + j) % this.phrase.length];
          this.right[j] = this.dryRight[j] = this.phrase[(this.frames + j + 97) % this.phrase.length];
        }
        this.api.opxy_effect_render(Math.floor(this.frames * 192 / 44100) >>> 0);
        this.frames += 64; this.cursor = 0;
      }
      // Smooth bypass/mix changes over ~5 ms, with a bounded output even if
      // an experimental effect writes full-scale Q31 samples.
      const target = this.bypass ? 0 : this.wet;
      this.blend += Math.max(-1 / 220, Math.min(1 / 220, target - this.blend));
      const at = this.cursor++, wet = this.blend;
      const l = (this.dryLeft[at] * (1 - wet) + this.left[at] * wet) / 0x7fffffff * 0.25;
      const r = (this.dryRight[at] * (1 - wet) + this.right[at] * wet) / 0x7fffffff * 0.25;
      output[i] = right === output ? (l + r) * 0.5 : l;
      if (right !== output) right[i] = r;
    }
  }
}
