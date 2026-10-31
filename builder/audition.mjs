import engineSource from '../sdk/browser/engine.mjs?raw';
import processorSource from '../sdk/browser/processor.mjs?raw';

// Reuse the SDK audition runner with the same authenticated C++ module as the
// display. Inline the worklet so the standalone HTML also works without a server.
const workletSource = engineSource.replace('export class ', 'class ') + '\n' +
  processorSource.replace("import { PreviewEngine } from './engine.mjs';", '').replace('export class ', 'class ') +
  "\nregisterProcessor('opxy-audition', PreviewProcessor);";

export class Audition {
  constructor(module, parameters, onState) {
    this.module = module; this.parameters = [...parameters]; this.onState = onState;
    this.volume = 0.5; this.revision = 0; this.disposed = false; this.playing = false;
    // The note a key holds, from its press: a release during audio setup still ends it.
    this.held = null;
    this.snapshotSerial = 0; this.snapshots = new Map();
  }
  takeAudio() {
    if (!this.processor || !this.audioReady || this.context?.state !== 'running') return Promise.resolve(null);
    const id = ++this.snapshotSerial;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.snapshots.delete(id); reject(new Error('Audio telemetry timed out')); }, 1000);
      this.snapshots.set(id, {resolve, reject, timer});
      this.processor.port.postMessage({type:'audioSnapshot', id, track:0});
    });
  }
  async start() {
    const context = this.context = new AudioContext({sampleRate:44100, latencyHint:'interactive'});
    if (context.sampleRate !== 44100) throw new Error('This browser cannot provide 44.1 kHz audio.');
    // Chrome rejects blob:null worklets from file://; a self-contained data URL
    // lets the offline export use the same runner without external requests.
    const offline = location.protocol === 'file:';
    const url = offline ? 'data:text/javascript;charset=utf-8,' + encodeURIComponent(workletSource)
      : URL.createObjectURL(new Blob([workletSource], {type:'text/javascript'}));
    try { await context.audioWorklet.addModule(url); }
    finally { if (!offline) URL.revokeObjectURL(url); }
    if (this.disposed) return;
    const processor = this.processor = new AudioWorkletNode(context, 'opxy-audition', {
      numberOfInputs:0, numberOfOutputs:1,
      outputChannelCount:[new WebAssembly.Instance(this.module).exports.opxy_output_channels?.() ?? 1],
      processorOptions:{module:this.module, parameters:this.parameters},
    });
    const gain = this.gain = context.createGain(); gain.gain.value = 0;
    processor.connect(gain).connect(context.destination);
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Audio preview did not start.')), 5000);
      const fail = message => { clearTimeout(timer); this.stop(); reject(new Error(message)); this.onState(false, message); };
      processor.onprocessorerror = () => fail('Audio preview stopped. Select the machine again to retry.');
      processor.port.onmessage = ({data}) => {
        if (data.ready) { this.audioReady = true; clearTimeout(timer); resolve(); }
        if (data.audioSnapshot !== undefined) {
          const pending = this.snapshots.get(data.audioSnapshot);
          if (pending) { clearTimeout(pending.timer); this.snapshots.delete(data.audioSnapshot); pending.resolve(data.snapshot); }
        }
        if (data.error) fail(data.error);
      };
    });
    context.onstatechange = () => { if (context.state !== 'running' && this.playing) this.stop(); };
  }
  async play(note) {
    if (this.disposed) return;
    const revision = ++this.revision;
    this.held = note;
    try {
      // Create/resume during the click gesture, before asynchronous worklet setup.
      this.starting ??= this.start();
      const resume = this.context?.resume() ?? Promise.resolve();
      await Promise.all([this.starting, resume]);
      if (this.disposed || revision !== this.revision) return;
      this.processor.port.postMessage({type:'panic'});
      this.processor.port.postMessage({type:'parameters', value:this.parameters});
      this.gain.gain.value = this.volume;
      this.processor.port.postMessage({type:'on', note, velocity:0.8});
      this.playing = true; this.onState(true);
      if (this.held !== note) this.processor.port.postMessage({type:'off', note});
    } catch (error) {
      if (this.disposed || revision !== this.revision) return;
      this.stop();
      this.processor?.disconnect(); this.processor?.port.close();
      await this.context?.close().catch(() => {});
      this.context = this.processor = this.gain = this.starting = null;
      this.onState(false, error.message);
    }
  }
  // A key let go: the note fades over the preview engine's short release.
  release(note) {
    if (this.held === note) this.held = null;
    if (this.playing) this.processor?.port.postMessage({type:'off', note});
  }
  stop() {
    this.revision++; this.playing = false;
    if (this.gain && this.context.state !== 'closed') this.gain.gain.value = 0;
    this.processor?.port.postMessage({type:'panic'});
    this.onState(false);
  }
  setParameters(values) {
    this.parameters = [...values];
    this.processor?.port.postMessage({type:'parameters', value:this.parameters});
  }
  setVolume(value) {
    this.volume = value;
    if (this.gain && this.playing) this.gain.gain.value = value;
  }
  destroy() {
    this.audioReady = false;
    for (const request of this.snapshots.values()) { clearTimeout(request.timer); request.resolve(null); }
    this.snapshots.clear();
    this.disposed = true; this.stop();
    this.processor?.disconnect(); this.processor?.port.close();
    this.context?.close().catch(() => {});
  }
}
