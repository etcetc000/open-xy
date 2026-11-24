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
      blockFrames: this.engine.blockFrames, outputChannels: this.engine.outputChannels, stateBytes: this.engine.stateBytes });
  }
  process(_inputs, outputs) {
    const channels = outputs[0];
    if (channels?.length) {
      // A stereo module fills both channels; a mono one is copied to the right.
      this.engine.render(channels[0], channels[1]);
    }
    return true;
  }
}
