import { PreviewEngine } from './engine.mjs';

export class PreviewProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { module, parameters } = options.processorOptions;
    this.engine = new PreviewEngine(module, sampleRate, parameters);
    this.port.onmessage = ({ data }) => {
      try {
        if (data.type === 'parameters') this.engine.setParameters(data.value);
        if (data.type === 'model') this.engine.setModel(data.value);
        if (data.type === 'on') this.engine.noteOn(data.note, data.velocity);
        if (data.type === 'off') this.engine.noteOff(data.note);
        if (data.type === 'panic') this.engine.panic();
      } catch (error) { this.port.postMessage({ error: error.message }); }
    };
    this.port.postMessage({ ready: true, backend: 'cpp-wasm', sampleRate,
      blockFrames: this.engine.blockFrames, stateBytes: this.engine.stateBytes });
  }
  process(_inputs, outputs) {
    const channels = outputs[0];
    if (channels?.length) {
      this.engine.render(channels[0]);
      for (let i = 1; i < channels.length; ++i) channels[i].set(channels[0]);
    }
    return true;
  }
}
