async function readPreviewManifest() {
  try {
    const response = await fetch('/preview/manifest.json');
    if (!response.ok) throw new Error('Build the C++ preview, then start the server with --preview. See the README.');
    return await response.json();
  } catch (error) {
    document.getElementById('status').textContent = error.message;
    document.getElementById('audio').disabled = true;
    throw error;
  }
}
const previewManifest = await readPreviewManifest();
const customPreview = previewManifest.kind === 'cpp-browser-custom-preview';
if (customPreview) {
  document.querySelector('.badge').textContent = 'LOCAL AUTHORING';
  document.getElementById('preview-mode').textContent = 'Local authoring preview. This audition does not qualify a firmware build.';
}
import { createControls, editedShown, shownControls, VIEWS } from '../sdk/browser/controls.mjs';
const controls = createControls(previewManifest.definition);
const { definition, parameters, positions, defaults, presets, displayValue, makePatch, readPatch, validateParameters, q31 } = controls;
import { renderScreen as renderBase } from './screen.mjs';
import { PreviewDisplay, SvgDisplay, NoteActivity } from '../sdk/browser/display.mjs';
const noteActivity = new NoteActivity();
const renderScreen = (...args) => renderBase(...args, controls, shown, scene?.header);
const $ = id => document.getElementById(id);
const controlId = id => customPreview ? `custom-control-${parameters.findIndex(p => p.id === id)}` : id;
// Page copy comes from the machine: its definition and machine.json description.
document.querySelector('.instrument-top h2').firstChild.textContent = definition.name + ' ';
document.title = definition.name + ' · Machine workbench';
document.querySelector('.instrument').setAttribute('aria-label', definition.name + ' instrument');
document.querySelector('.intro h1').textContent = definition.name;
if (previewManifest.description) document.querySelector('.intro .lede').textContent = previewManifest.description;
if (!Object.keys(presets).length) document.querySelector('#presets').closest('.side-section').hidden = true;
if (customPreview) {
  document.querySelector('.intro .eyebrow').textContent = 'LOCAL MACHINE / AUTHORING';
  document.querySelector('.intro .lede').textContent = 'Edit your C++ sound and display, then rebuild to hear and see the change.';
}
// The parameter each control position shows: the one last edited there.
let values = defaults(), view = VIEWS[0], octave = 3, shown = [0, 1, 2, 3];
let visualNotes = 0, display = null, scene = null, displayBackend = null;
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
let context, processor, gain, analyser, samples, starting;
let audioBackend = null;
let audioAvailable = true;
const parameterWords = () => q31(values);
const held = new Map(), timers = new Set();
const keys = ['a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j', 'k'];
const black = new Set([1, 3, 6, 8, 10]);
const status = text => { $('status').textContent = text; };
const send = message => processor?.port.postMessage(message);

for (const [i, param] of parameters.entries()) {
  const id = controlId(param.id);
  const item = document.createElement('div');
  item.className = 'control';
  item.innerHTML = `<label for="${id}">${positions[i] + 1} / ${param.label}</label><svg class="dial" viewBox="0 0 60 60" aria-hidden="true"><circle cx="30" cy="30" r="27"/><line id="dial-${id}" x1="30" y1="9" x2="30" y2="18"/></svg><output id="value-${id}" for="${id}"></output><input id="${id}" type="range" min="0" max="1" step="0.001" aria-label="${param.label}">`;
  $('controls').append(item);
  $(id).addEventListener('input', event => {
    values[param.id] = Number(event.target.value);
    shown = editedShown(shown, positions, i, previewManifest.definition.press === 'page');
    update();
  });
}
for (const [name, preset] of Object.entries(presets)) {
  const button = document.createElement('button');
  button.textContent = name;
  button.addEventListener('click', () => { values = { ...preset }; update(); status(`${name} loaded`); });
  $('presets').append(button);
}
function draw(elapsed = 0) {
  if (display) {
    if (!audioAvailable) display.setActivity(new Set(held.values()).size);
    const input = shownControls(parameterWords(), shown);
    try {
      scene.apply(display.tick(input, elapsed, visualNotes, 0, reducedMotion.matches, noteActivity));
    }
    catch (error) { display = scene = displayBackend = null; status(`Display stopped: ${error.message}`); }
  }
  $('screen').innerHTML = renderScreen(values, view, samples, scene?.svg() || '');
}
async function loadPreview() {
  const manifest = previewManifest;
  const responses = await Promise.all([fetch('/preview/engine.wasm'), fetch('/preview/panel.svg')]);
  if (responses.some(response => !response.ok)) throw new Error('Compiled preview or artwork is unavailable.');
  const [bytes, artwork] = await Promise.all(responses.map(response => response.arrayBuffer()));
  const digestOf = async buffer => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', buffer)),
    byte => byte.toString(16).padStart(2, '0')).join('');
  const [digest, artDigest] = await Promise.all([digestOf(bytes), digestOf(artwork)]);
  if (digest !== manifest.wasm_sha256 || artDigest !== manifest.graphics.asset_sha256) throw new Error('Preview identity mismatch.');
  const module = await WebAssembly.compile(bytes);
  audioAvailable = WebAssembly.Module.exports(module).some(entry => entry.name === 'opxy_render');
  const readyDisplay = new PreviewDisplay(module);
  const readyScene = new SvgDisplay(new TextDecoder().decode(artwork), manifest.graphics);
  display = readyDisplay;
  scene = readyScene;
  displayBackend = { backend: 'cpp-wasm', stateBytes: display.stateBytes, frameBytes: readyDisplay.frame.byteLength, wasmSha256: digest };
  if (!audioAvailable) {
    $('audio').disabled = true; $('audio').textContent = 'Display only';
    $('monitor').disabled = true;
    $('preview-mode').textContent = 'Interactive screen preview. Sample playback and loading run on the OP-XY.';
    status('Screen ready. Use the keys to animate; this preview has no audio.');
  }
  draw();
  return { manifest, digest, module };
}
let preview;
function update() {
  for (const p of parameters) {
    const id = controlId(p.id);
    $(id).value = values[p.id];
    $(id).setAttribute('aria-valuetext', displayValue(p.id, values));
    $(`value-${id}`).textContent = displayValue(p.id, values);
    $(`dial-${id}`).setAttribute('transform', `rotate(${-135 + 270 * values[p.id]} 30 30)`);
  }
  for (const button of $('presets').children) {
    button.setAttribute('aria-pressed', String(parameters.every(p => presets[button.textContent][p.id] === values[p.id])));
  }
  for (const mode of VIEWS) $(mode).setAttribute('aria-pressed', String(mode === view));
  send({ type: 'parameters', value: parameterWords() });
  draw();
}
async function enableAudio() {
  if (!audioAvailable) return false;
  if (starting) return starting;
  starting = (async () => {
    $('audio').disabled = true;
    try {
      if (!context) {
        const { manifest, digest, module } = await preview;
        context = new AudioContext({ latencyHint: 'interactive', sampleRate: manifest.sample_rate });
        if (context.sampleRate !== manifest.sample_rate) throw new Error('Browser cannot provide the kernel sample rate.');
        await context.audioWorklet.addModule('/harness/processor.mjs');
        processor = new AudioWorkletNode(context, 'opxy-engine', { numberOfInputs: 0, numberOfOutputs: 1,
          outputChannelCount: [1], processorOptions: { module, parameters: parameterWords() } });
        const ready = new Promise((resolve, reject) => {
          const timeout = setTimeout(() => reject(new Error('Audio processor did not start.')), 5000);
          processor.onprocessorerror = () => {
            clearTimeout(timeout); reject(new Error('Audio processor failed.'));
            audioBackend = null; panic(); status('Audio processor stopped. Reload to retry.');
          };
          processor.port.onmessage = ({ data }) => {
            if (data.ready) {
              clearTimeout(timeout); audioBackend = { ...data, wasmSha256: digest }; resolve();
            }
            if (data.error) status(data.error);
          };
        });
        gain = context.createGain(); gain.gain.value = Number($('monitor').value);
        analyser = context.createAnalyser(); analyser.fftSize = 1024;
        samples = new Float32Array(analyser.fftSize);
        processor.connect(gain).connect(analyser).connect(context.destination);
        const activeContext = context;
        context.onstatechange = () => {
          if (context !== activeContext) return;
          $('audio').textContent = context.state === 'running' ? 'Mute audio' : 'Enable audio';
          if (context.state !== 'running') panic();
        };
        send({ type: 'parameters', value: parameterWords() });
        await Promise.all([context.resume(), ready]);
      }
      await context.resume();
      $('audio').textContent = 'Mute audio';
      status(`Audio on · ${context.sampleRate.toLocaleString()} Hz · 8 preview voices`);
      return true;
    } catch (error) {
      if (context) await context.close().catch(() => {});
      context = processor = gain = analyser = samples = null;
      audioBackend = null;
      $('audio').textContent = 'Enable audio';
      status(`Audio unavailable: ${error.message}`);
      return false;
    } finally { $('audio').disabled = false; }
  })();
  try { return await starting; } finally { starting = null; }
}
function paintKeys() {
  for (let i = 0; i < keys.length; i++) {
    const active = [...held.values()].includes((octave + 1) * 12 + i);
    $(`key-${i}`).classList.toggle('active', active);
    $(`key-${i}`).setAttribute('aria-pressed', String(active));
  }
}
function noteOn(source, note, velocity = 0.8) {
  if (!Number.isInteger(note) || note < 0 || note > 127 || !Number.isFinite(velocity) || velocity <= 0 || velocity > 1) return false;
  if (audioAvailable && context?.state !== 'running') { status('Enable audio first, then play a key.'); return false; }
  if (held.has(source)) return false;
  const existing = [...held.values()].includes(note);
  held.set(source, note);
  if (!existing) {
    send({ type: 'on', note, velocity });
    noteActivity.noteOn(note);
    visualNotes = (visualNotes + 1) >>> 0;
    draw();
  }
  paintKeys();
  return true;
}
function noteOff(source) {
  const note = held.get(source);
  held.delete(source);
  if (note !== undefined && ![...held.values()].includes(note)) {
    send({ type: 'off', note });
    noteActivity.noteOff(note);
    draw();
  }
  paintKeys();
  if (!audioAvailable) draw();
}
function panic() {
  for (const timer of timers) clearTimeout(timer);
  timers.clear(); held.clear(); send({ type: 'panic' });
  noteActivity.panic(); draw();
  paintKeys(); $('phrase').textContent = 'Play phrase';
}
for (const [i, key] of keys.entries()) {
  const button = document.createElement('button');
  button.id = `key-${i}`; button.className = `key ${black.has(i) ? 'black' : 'white'}`;
  button.textContent = key.toUpperCase();
  button.setAttribute('aria-label', `Play semitone ${i}, keyboard ${key.toUpperCase()}`);
  button.addEventListener('pointerdown', event => {
    event.preventDefault(); button.focus({ preventScroll: true }); button.setPointerCapture(event.pointerId);
    noteOn(`pointer-${event.pointerId}`, (octave + 1) * 12 + i);
  });
  for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) button.addEventListener(type, event => noteOff(`pointer-${event.pointerId}`));
  button.addEventListener('keydown', event => {
    if ([' ', 'Enter'].includes(event.key)) { event.preventDefault(); if (!event.repeat) noteOn(`button-${i}`, (octave + 1) * 12 + i); }
  });
  button.addEventListener('keyup', event => { if ([' ', 'Enter'].includes(event.key)) noteOff(`button-${i}`); });
  button.addEventListener('blur', () => noteOff(`button-${i}`));
  $('keyboard').append(button);
}
window.addEventListener('keydown', event => {
  if (event.key === 'Escape') { panic(); return; }
  if (event.ctrlKey || event.metaKey || event.altKey || event.repeat || /INPUT|SELECT|TEXTAREA/.test(event.target.tagName)) return;
  const i = keys.indexOf(event.key.toLowerCase());
  if (i >= 0) { event.preventDefault(); noteOn(`key-${event.code}`, (octave + 1) * 12 + i); }
});
window.addEventListener('keyup', event => noteOff(`key-${event.code}`));
window.addEventListener('blur', panic);
document.addEventListener('visibilitychange', () => { if (document.hidden) panic(); });
$('audio').addEventListener('click', async () => {
  if (context?.state === 'running') { panic(); await context.suspend(); status('Audio muted'); }
  else await enableAudio();
});
$('panic').addEventListener('click', () => { panic(); status('All notes stopped'); });
$('monitor').addEventListener('input', () => gain?.gain.setTargetAtTime(Number($('monitor').value), context.currentTime, 0.01));
for (const mode of VIEWS) $(mode).addEventListener('click', () => { view = mode; update(); });
$('zoom').addEventListener('change', () => $('frame').classList.toggle('native', $('zoom').value === 'native'));
for (const [id, direction] of [['octave-down', -1], ['octave-up', 1]]) $(id).addEventListener('click', () => {
  panic(); octave = Math.max(1, Math.min(7, octave + direction)); $('octave-label').textContent = `C${octave}–C${octave + 1}`;
});
function later(callback, ms) {
  const timer = setTimeout(() => { timers.delete(timer); callback(); }, ms); timers.add(timer);
}
$('phrase').addEventListener('click', () => {
  if (timers.size) { panic(); return; }
  if (audioAvailable && context?.state !== 'running') { status('Enable audio first to play the phrase.'); return; }
  panic(); $('phrase').textContent = 'Stop phrase';
  [0, 7, 12, 3, 10, 7, 2, 0].forEach((offset, i) => {
    later(() => noteOn(`phrase-${i}`, (octave + 1) * 12 + offset), i * 280);
    later(() => noteOff(`phrase-${i}`), i * 280 + 220);
  });
  later(() => { $('phrase').textContent = 'Play phrase'; }, 2240);
});
function download(name, body, type) {
  const url = URL.createObjectURL(new Blob([body], { type }));
  const anchor = document.createElement('a'); anchor.href = url; anchor.download = name; anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$('export').addEventListener('click', () => download(`${definition.id}.patch.json`, JSON.stringify(makePatch(values, view), null, 2) + '\n', 'application/json'));
$('svg').addEventListener('click', () => download(`${definition.id}.screen.svg`, renderScreen(values, view, samples, scene?.svg() || ''), 'image/svg+xml'));
$('import').addEventListener('click', () => $('file').click());
$('file').addEventListener('change', async () => {
  const file = $('file').files[0];
  if (!file) return;
  try {
    if (file.size > 16384) throw new Error('Patch exceeds 16 KB.');
    const patch = readPatch(JSON.parse(await file.text()));
    panic(); values = patch.parameters; view = patch.view; update(); status('Patch imported');
  } catch (error) { status(`Import rejected: ${error.message}`); }
  finally { $('file').value = ''; }
});
$('reset').addEventListener('click', () => { panic(); values = defaults(); shown = [0, 1, 2, 3]; update(); status('Controls reset'); });
// Stable local automation surface: uses the same validation and note lifecycle as UI.
window.machineLab = Object.freeze({
  getPatch: () => makePatch(values, view),
  loadPatch: patch => { const p = readPatch(patch); panic(); values = p.parameters; view = p.view; update(); },
  setParameters: next => { values = validateParameters(next); update(); },
  noteOn: (note, velocity = 0.8) => noteOn(`api-${note}`, note, velocity),
  noteOff: note => noteOff(`api-${note}`), panic,
  getStatus: () => ({ audio: context?.state || 'off', heldNotes: held.size, sampleRate: context?.sampleRate || null,
    audioBackend, displayBackend,
    visualNotes, noteActivity: { held_notes: display?.input[17] ?? null, note_off_serial: display?.input[18] ?? null },
    rms: samples ? Math.sqrt(samples.reduce((sum, x) => sum + x * x, 0) / samples.length) : 0 }),
});
update();
preview = loadPreview();
preview.catch(error => status(`Preview unavailable: ${error.message}`));
reducedMotion.addEventListener('change', () => draw());
let lastFrame = 0;
function animate(time) {
  if (!document.hidden && time - lastFrame > 32) {
    const elapsed = Math.min(8192, Math.max(0, Math.round((time - lastFrame) * 1.024)));
    lastFrame = time;
    if (analyser && context?.state === 'running') analyser.getFloatTimeDomainData(samples);
    else samples?.fill(0);
    if (view === 'waveform' || !reducedMotion.matches) draw(elapsed);
  }
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
