import { PreviewEngine } from './engine.mjs';

export class PreviewProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { module, parameters } = options.processorOptions;
    this.engine = new PreviewEngine(module, sampleRate, parameters);
    this.port.onmessage = ({ data }) => {
      try {
        if (data.type === 'parameters') this.engine.setParameters(data.value,data.track??0);
        if (data.type === 'on') this.engine.noteOn(data.note, data.velocity,data.track??0);
        if (data.type === 'off') this.engine.noteOff(data.note,data.track??0);
        if (data.type === 'panic') this.engine.panic();
        if (data.type === 'audioSnapshot') this.port.postMessage({audioSnapshot: data.id,
          snapshot: this.engine.takeAudio(data.track ?? 0, data.reset ?? false, data.resetTarget ?? 1)});
      } catch (error) { this.port.postMessage({ error: error.message }); }
    };
    this.port.postMessage({ ready: true, backend: 'cpp-wasm', sampleRate,
      renderPath:'ordinary64',voiceCapacity:this.engine.voices.length,
      blockFrames: this.engine.blockFrames, outputChannels: this.engine.outputChannels, stateBytes: this.engine.stateBytes });
  }
  process(_inputs, outputs) {
    const channels = outputs[0];
    if (channels?.length) {
      this.engine.render(channels[0], channels[1]);
    }
    return true;
  }
}
