// Host audition only. The WASM kernel owns sound; this runner owns eight voices,
// pitch buffers, a simple audition gate and mixing. No stock FX emulation.
function bounded(value, low, high, name) {
  if (!Number.isInteger(value) || value < low || value > high) throw new Error(`Invalid ${name}`);
  return value;
}

export class PreviewEngine {
  constructor(module, sampleRate, parameters) {
    if (!(module instanceof WebAssembly.Module)) throw new Error('A compiled preview module is required');
    if (WebAssembly.Module.imports(module).length) throw new Error('Preview modules must be self-contained');
    this.api = new WebAssembly.Instance(module).exports;
    const api = this.api;
    if (api.opxy_preview_abi() !== 1) throw new Error('Unsupported preview ABI');
    this.sampleRate = bounded(api.opxy_sample_rate(), 8000, 192000, 'kernel sample rate');
    if (sampleRate !== this.sampleRate) throw new Error(`Kernel requires ${this.sampleRate} Hz`);
    this.blockFrames = bounded(api.opxy_block_frames(), 1, 256, 'block length');
    // Older ABI-1 modules are mono. Stereo output is opt-in per module.
    this.outputChannels = bounded(api.opxy_output_channels?.() ?? 1, 1, 2, 'output channels');
    this.audioCapacity = bounded(api.opxy_audio_capacity?.() ?? 16, 16, 256, 'audio telemetry capacity');
    if (![16,256].includes(this.audioCapacity)) throw new Error('Invalid audio telemetry capacity');
    this.audioWords = bounded(api.opxy_audio_words?.() ?? 0, 0, this.audioCapacity, 'audio telemetry words');
    this.stateBytes = bounded(api.opxy_state_bytes(), 4, api.memory.buffer.byteLength, 'voice state size');
    this.parameterCount = bounded(api.opxy_parameter_count(), 1, 8, 'parameter count');
    const alignment = bounded(api.opxy_state_alignment(), 4, 64, 'voice alignment');
    if (alignment & (alignment - 1)) throw new Error('Voice alignment must be a power of two');
    const voiceCount = bounded(api.opxy_voice_capacity(), 1, 8, 'audition voice count');
    const peak = bounded(api.opxy_output_peak_q31(), 1, 0x7fffffff, 'kernel headroom');
    this.auditionGain = 0.1 / peak;
    const memory = api.memory.buffer;
    let next = bounded(Number(api.__heap_base.value), 0, memory.byteLength, 'heap base');
    const allocate = (bytes, align = 4) => {
      const address = Math.ceil(next / align) * align;
      next = address + bytes;
      if (next > memory.byteLength) throw new Error('Preview memory budget exceeded');
      return address;
    };
    this.parameterAddress = allocate(this.parameterCount * 4);
    this.parameters = new Int32Array(memory, this.parameterAddress, this.parameterCount);
    this.audioAddress = this.audioWords ? allocate(this.audioCapacity * 4) : 0;
    this.audioSnapshot = this.audioWords ? new Uint32Array(memory, this.audioAddress, this.audioCapacity) : null;
    this.voices = Array.from({ length: voiceCount }, (_, index) => {
      const state = bounded(api.opxy_voice_state(index), 1, Number(api.__heap_base.value) - this.stateBytes,
                            'voice address');
      if (state % alignment) throw new Error('Misaligned voice state');
      const pitchAddress = allocate(this.blockFrames * 4);
      const outputAddress = allocate(this.blockFrames * this.outputChannels * 4);
      api.opxy_initialize(state);
      return { state, pitchAddress, outputAddress,
        pitch: new Uint32Array(memory, pitchAddress, this.blockFrames),
        output: new Int32Array(memory, outputAddress, this.blockFrames * this.outputChannels),
        active: false, note: 0, age: 0, velocity: 0, amplitude: 0, releasing: false,
        cursor: this.blockFrames };
    });
    for (let i = 0; i < voiceCount; ++i) for (let j = 0; j < i; ++j) {
      if (Math.abs(this.voices[i].state - this.voices[j].state) < this.stateBytes) {
        throw new Error('Overlapping voice state');
      }
    }
    this.clock = 0;
    this.attackStep = 1 / (sampleRate * 0.005);
    this.releaseStep = 1 / (sampleRate * 0.12);
    this.setParameters(parameters);
  }

  // Called by the worklet's message handler between audio quanta, never by render.
  takeAudio(track = 0, reset = false, target = 1) {
    if (typeof reset !== "boolean") throw new Error("Invalid audio reset request");
    bounded(track, 0, 7, 'audio track');
    if (reset) bounded(target, 1, 0xffffffff, 'audio reset target');
    if (!this.audioWords) return {count: 0, words: Array(16).fill(0)};
    if (reset) this.api.opxy_audio_reset?.(track, target);
    const count = this.api.opxy_take_audio(track, this.audioAddress);
    if (count !== this.audioWords) throw new Error('Invalid audio telemetry snapshot');
    if (!this.api.opxy_audio_sequence) return {count, words:Array.from(this.audioSnapshot)};
    return {count, words: Array.from(this.audioSnapshot), sequence:this.api.opxy_audio_sequence?.(track) ?? 0,
      requested:this.api.opxy_audio_requested?.(track) ?? 0, applied:this.api.opxy_audio_applied?.(track) ?? 0,
      resetSupported:this.api.opxy_audio_reset_supported?.() ?? 0};
  }

  setParameters(values) {
    if (!Array.isArray(values) || values.length !== this.parameterCount ||
        values.some(v => !Number.isInteger(v) || v < 0 || v > 0x7fffffff)) {
      throw new Error('Expected the defined nonnegative Q31 parameter words');
    }
    this.parameters.set(values);
    // A partially consumed kernel block finishes before new controls apply.
    // At 44.1 kHz the bound is 63 samples (1.43 ms); never restart voice phase.
  }

  noteOn(note, velocity = 0.8) {
    if (!Number.isInteger(note) || note < 0 || note > 127 ||
        !Number.isFinite(velocity) || velocity <= 0 || velocity > 1) return false;
    let voice = this.voices.find(v => v.active && v.note === note) || this.voices.find(v => !v.active);
    if (!voice) voice = this.voices.reduce((a, b) => a.age < b.age ? a : b);
    if (!voice.active || voice.note !== note) {
      this.api.opxy_destroy(voice.state);
      this.api.opxy_initialize(voice.state);
    }
    this.api.opxy_start(voice.state);
    voice.pitch.fill(Math.round(440 * 2 ** ((note - 69) / 12) / this.sampleRate * 0x100000000));
    voice.active = true; voice.note = note; voice.age = ++this.clock;
    voice.velocity = velocity; voice.amplitude = 0; voice.releasing = false;
    voice.cursor = this.blockFrames;
    return true;
  }
  noteOff(note) {
    for (const voice of this.voices) if (voice.active && voice.note === note && !voice.releasing) {
      // ABI-1 modules made before the opt-in release extension omit this export.
      this.api.opxy_release?.(voice.state);
      voice.releasing = true;
    }
  }
  panic() { for (const voice of this.voices) {
    if (voice.active) this.api.opxy_destroy(voice.state);
    voice.active = false;
  } }

  render(output, right) {
    if (this.outputChannels === 2 && (!right || right.length !== output.length)) {
      throw new Error('Stereo preview requires equal left and right output buffers');
    }
    // No typed-array creation, memory growth, transcendental functions or messages
    // here. Each voice retains its partial block across arbitrary host quanta.
    for (let i = 0; i < output.length; ++i) {
      let sample = 0, sampleRight = 0;
      for (let j = 0; j < this.voices.length; ++j) {
        const voice = this.voices[j];
        if (!voice.active) continue;
        voice.amplitude = voice.releasing ? Math.max(0, voice.amplitude - this.releaseStep)
                                          : Math.min(1, voice.amplitude + this.attackStep);
        if (voice.releasing && voice.amplitude === 0) {
          this.api.opxy_destroy(voice.state);
          voice.active = false; continue;
        }
        if (voice.cursor === this.blockFrames) {
          this.api.opxy_render(voice.state, voice.outputAddress, this.parameterAddress, voice.pitchAddress);
          voice.cursor = 0;
        }
        // Compiled headroom determines audition normalization, peak 0.1/voice.
        const at = voice.cursor++ * this.outputChannels;
        sample += voice.output[at] * this.auditionGain * voice.amplitude * voice.velocity;
        if (this.outputChannels === 2) {
          sampleRight += voice.output[at + 1] * this.auditionGain * voice.amplitude * voice.velocity;
        }
      }
      output[i] = sample;
      if (right) right[i] = this.outputChannels === 2 ? sampleRight : sample;
    }
  }
}
