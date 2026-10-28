// Optional real-browser check. Attach only to a dedicated local test Chrome.
// Chrome: --headless=new --remote-debugging-port=9226 --user-data-dir=<private test profile>
// Start tools/serve_harness.py first. No npm dependencies (Node 22+).
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createControls } from '../sdk/browser/controls.mjs';

function port(name, fallback) {
  const value = Number(process.env[name] || fallback);
  assert.ok(Number.isInteger(value) && value > 0 && value <= 65535, `Invalid ${name}`);
  return value;
}
const endpoint = `http://127.0.0.1:${port('OPXY_CHROME_PORT', 9226)}`;
const preview = `http://127.0.0.1:${port('OPXY_PREVIEW_PORT', 8917)}`;
const manifest = await (await fetch(preview + '/preview/manifest.json')).json();
const module = await WebAssembly.compile(await (await fetch(preview + '/preview/engine.wasm')).arrayBuffer());
const hasAudio = WebAssembly.Module.exports(module).some(entry => entry.name === 'opxy_render');
// Expectations come from the served definition; nothing here names a machine.
const engineId = manifest.engine;
const controls = createControls(structuredClone(manifest.definition));
const custom = manifest.kind === 'cpp-browser-custom-preview';
const controlId = (id, i) => custom ? `custom-control-${i}` : id;
const first = controls.parameters[0];
const firstId = controlId(first.id, 0);
const shown = id => evaluate(`document.getElementById(${JSON.stringify('value-' + id)}).textContent`);
const target = await (await fetch(`${endpoint}/json/new?about:blank`, { method: 'PUT' })).json();
const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
let sequence = 0;
const pending = new Map(), errors = [];
socket.onmessage = ({ data }) => {
  const message = JSON.parse(data);
  if (message.id) {
    const waiter = pending.get(message.id);
    pending.delete(message.id);
    clearTimeout(waiter.timer);
    if (message.error) waiter.reject(new Error(JSON.stringify(message.error))); else waiter.resolve(message.result);
  } else if (message.method === 'Runtime.exceptionThrown' || message.method === 'Log.entryAdded' && message.params.entry.level === 'error') errors.push(message);
};
function command(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++sequence;
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`Timeout: ${method}`)); }, 10000);
    pending.set(id, { resolve, reject, timer }); socket.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function click(id) {
  const point = await evaluate(`(() => { const r = document.getElementById(${JSON.stringify(id)}).getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })()`);
  await command('Input.dispatchMouseEvent', { type: 'mousePressed', ...point, button: 'left', clickCount: 1 });
  await command('Input.dispatchMouseEvent', { type: 'mouseReleased', ...point, button: 'left', clickCount: 1 });
}
const screenshots = fileURLToPath(new URL(`../local/harness-review-${Date.now()}/`, import.meta.url));
mkdirSync(screenshots, { recursive: true });
try {
  await command('Runtime.enable'); await command('Log.enable'); await command('Page.enable');
  await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1180, deviceScaleFactor: 1, mobile: false });
  await command('Page.navigate', { url: preview + '/' });
  for (let attempt = 0; attempt < 50; attempt++) {
    if (await evaluate('Boolean(window.machineLab?.getStatus().displayBackend)')) break;
    await delay(100);
  }
  assert.ok(await evaluate('Boolean(window.machineLab?.getStatus().displayBackend)'), JSON.stringify(errors));
  assert.equal(await evaluate('machineLab.getStatus().audio'), 'off');
  for (const [i, p] of controls.parameters.entries()) {
    assert.equal(await shown(controlId(p.id, i)), controls.displayValue(p.id, controls.defaults()));
  }
  if (hasAudio) {
  await click('audio'); await delay(700);
  assert.equal(await evaluate('machineLab.getStatus().audio'), 'running');
  assert.equal(await evaluate('document.getElementById("audio").textContent'), 'Mute audio');
  } else {
    assert.equal(await evaluate('document.getElementById("audio").disabled'), true);
    assert.equal(await evaluate('document.getElementById("audio").textContent'), 'Display only');
  }
  await command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'a', code: 'KeyA' }); await delay(150);
  const sounding = await evaluate('machineLab.getStatus()');
  if (hasAudio) {
  assert.equal(sounding.sampleRate, 44100);
  assert.equal(sounding.audioBackend.backend, 'cpp-wasm');
  assert.equal(sounding.audioBackend.blockFrames, 64);
  assert.equal(sounding.displayBackend.backend, 'cpp-wasm');
  assert.ok(sounding.displayBackend.frameBytes > 0);
  assert.equal(sounding.displayBackend.wasmSha256, sounding.audioBackend.wasmSha256);
  assert.equal(sounding.audioBackend.wasmSha256,
    (await (await fetch(preview + '/preview/manifest.json')).json()).wasm_sha256);
  assert.ok(sounding.rms > 0.001);
  } else {
    assert.equal(sounding.audio, 'off');
    assert.equal(sounding.audioBackend, null);
    assert.equal(sounding.rms, 0);
    assert.equal(sounding.displayBackend.backend, 'cpp-wasm');
  }
  assert.equal(sounding.heldNotes, 1);
  assert.equal(sounding.visualNotes, 1);
  assert.deepEqual(sounding.noteActivity, {held_notes:1,note_off_serial:0});
  await command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'a', code: 'KeyA' }); await delay(300);
  assert.equal(await evaluate('machineLab.getStatus().heldNotes'), 0);
  assert.ok(await evaluate('machineLab.getStatus().rms < 0.000001'));
  assert.deepEqual(await evaluate('machineLab.getStatus().noteActivity'), {held_notes:0,note_off_serial:1});
  // Gate snapshots reach C++ immediately, including while motion is reduced.
  await command('Emulation.setEmulatedMedia', {features:[{name:'prefers-reduced-motion',value:'reduce'}]});
  await evaluate('machineLab.noteOn(60); machineLab.noteOn(64); machineLab.noteOff(60)');
  assert.deepEqual(await evaluate('machineLab.getStatus().noteActivity'), {held_notes:1,note_off_serial:2});
  await evaluate('machineLab.noteOff(60)');
  assert.deepEqual(await evaluate('machineLab.getStatus().noteActivity'), {held_notes:1,note_off_serial:2});
  await evaluate('machineLab.panic()');
  assert.deepEqual(await evaluate('machineLab.getStatus().noteActivity'), {held_notes:0,note_off_serial:3});
  await command('Emulation.setEmulatedMedia', {features:[]});
  for (const value of [1, 0.3, 0.301]) {
    await evaluate(`(() => { const el = document.getElementById(${JSON.stringify(firstId)}); el.value = ${value}; el.dispatchEvent(new Event('input')); })()`);
    assert.equal(await evaluate(`machineLab.getPatch().parameters[${JSON.stringify(first.id)}]`), value);
    assert.equal(await shown(firstId), controls.displayValue(first.id, { ...controls.defaults(), [first.id]: value }));
  }
  assert.ok(await evaluate(`(() => { const p = machineLab.getPatch(); p.parameters[${JSON.stringify(first.id)}] = 2; try { machineLab.loadPatch(p); return false; } catch { return true; } })()`));
  await evaluate(`(() => { const p = machineLab.getPatch(); machineLab.loadPatch(p); })()`);
  await command('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: screenshots });
  await click('export'); await click('svg');
  for (let attempt = 0; attempt < 40; attempt++) {
    if (existsSync(`${screenshots}/${engineId}.patch.json`) && existsSync(`${screenshots}/${engineId}.screen.svg`)) break;
    await delay(100);
  }
  const savedPatch = JSON.parse(readFileSync(`${screenshots}/${engineId}.patch.json`, 'utf8'));
  assert.deepEqual(savedPatch, await evaluate('machineLab.getPatch()'));
  assert.match(readFileSync(`${screenshots}/${engineId}.screen.svg`, 'utf8'), /width="480" height="220"/);
  await click('reset');
  const dom = await command('DOM.getDocument');
  const fileInput = await command('DOM.querySelector', { nodeId: dom.root.nodeId, selector: '#file' });
  await command('DOM.setFileInputFiles', { nodeId: fileInput.nodeId, files: [`${screenshots}/${engineId}.patch.json`] });
  await delay(150);
  assert.deepEqual(await evaluate('machineLab.getPatch()'), savedPatch);
  writeFileSync(`${screenshots}/invalid.patch.json`, '{"kind":"wrong"}');
  await command('DOM.setFileInputFiles', { nodeId: fileInput.nodeId, files: [`${screenshots}/invalid.patch.json`] });
  await delay(150);
  assert.match(await evaluate('document.getElementById("status").textContent'), /Import rejected/);
  assert.deepEqual(await evaluate('machineLab.getPatch()'), savedPatch);
  await click('reset'); await click('phrase'); await delay(100);
  assert.equal(await evaluate('document.getElementById("phrase").textContent'), 'Stop phrase');
  await click('panic'); await delay(300);
  assert.equal(await evaluate('machineLab.getStatus().heldNotes'), 0);
  assert.ok(await evaluate('machineLab.getStatus().rms < 0.000001'));
  await evaluate('machineLab.noteOn(60)');
  await evaluate('window.dispatchEvent(new Event("blur"))');
  assert.equal(await evaluate('machineLab.getStatus().heldNotes'), 0);
  await click('waveform');
  assert.ok(await evaluate('Boolean(document.querySelector("#screen polyline"))'));
  await click('routing');
  const preset = Object.values(controls.presets).at(-1) ?? controls.defaults();
  await evaluate(`machineLab.setParameters(${JSON.stringify(preset)}); machineLab.noteOn(60)`);
  await delay(240);
  const desktop = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
  writeFileSync(`${screenshots}/desktop.png`, Buffer.from(desktop.data, 'base64'));
  await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  await delay(150);
  assert.ok(await evaluate('document.documentElement.scrollWidth <= innerWidth'));
  const mobile = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
  writeFileSync(`${screenshots}/mobile.png`, Buffer.from(mobile.data, 'base64'));
  await evaluate('machineLab.noteOff(60); machineLab.panic()');
  if (hasAudio) {
    await click('audio'); await delay(100);
    assert.equal(await evaluate('machineLab.getStatus().audio'), 'suspended');
  }
  // Missing favicon is harmless; all script, worklet and runtime errors fail.
  const relevant = errors.filter(e => !JSON.stringify(e).includes('favicon.ico'));
  assert.deepEqual(relevant, []);
  console.log(JSON.stringify({ result: 'PASS', mode: hasAudio ? 'audio' : 'silent display', checks: ['preview mode', 'keyboard press and release', 'control display', 'control mapping', 'patch/SVG download', 'patch import and rejection', 'phrase/panic', 'blur release', 'screen view', 'mobile overflow', 'browser errors'], sounding, screenshots }, null, 2));
} finally {
  socket.close();
  await fetch(`${endpoint}/json/close/${target.id}`);
}
