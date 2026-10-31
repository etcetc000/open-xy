import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import * as Core from '../builder/core.mjs';
import {createGraphicsConfiguration} from '../builder/graphics-configuration.mjs';
import {createSnapshotService} from '../builder/machine-snapshots.mjs';
import {createGraphicsDialog} from '../builder/graphics-dialog.mjs';
import {shownControls} from '../sdk/browser/controls.mjs';
import {headerSvg} from '../sdk/browser/display.mjs';

// Exercise the imported dialog, state model and snapshot service together.
// The retained renderer and WASM transport have separate contract tests.
class Element {
  constructor(tag = 'div') { this.tag = tag; }
  children = []; attributes = {}; dataset = {}; listeners = {};
  textContent = ''; hidden = false; open = false;
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  setAttribute(key, value) { this.attributes[key] = value; }
  removeAttribute(key) { delete this.attributes[key]; }
  addEventListener(type, handler) { this.listeners[type] = handler; }
  removeEventListener(type) { delete this.listeners[type]; }
  focus() {}
  showModal() { assert.equal(this.open, false); this.open = true; }
  close() { this.open = false; this.listeners.close?.(); }
}

const original = '<g id="anim_panel"><circle id="anim_node_0" cx="0" cy="0" r="10" fill="#ff3300" stroke="#00aaff" stroke-width="1"/><circle id="anim_palette_0" cx="640" cy="0" r="1" fill="#00aaff" stroke="#ff3300" stroke-width="1"/></g>';
const grayscale = original.replaceAll('#ff3300', '#bbbbbb').replaceAll('#00aaff', '#666666');
const wasm = Uint8Array.of(0, 97, 115, 109, 1, 0, 0, 0); // Public empty WASM module.
const screenTemplate = readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg', import.meta.url), 'utf8');
const machine = {
  id:'animated', name:'Animated', artwork:original, grayscaleArtwork:grayscale,
  controls:['Blend', 'Width', 'Level', 'Motion'],
  preview:{wasm:Buffer.from(wasm).toString('base64'), sha256:await Core.sha256(wasm), defaults:[0, 1073741824, 1395864371, 1073741824],
    graphics:{nodes:['anim_node_0'], texts:[], palette:['anim_palette_0'], paths:0, points:0}},
};

async function harness({machines = [machine], delayed = false} = {}) {
  const html = readFileSync(new URL('../builder/index.html', import.meta.url), 'utf8');
  const elements = new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(([, id]) => [id, new Element()]));
  const $ = id => { assert.ok(elements.has(id), id); return elements.get(id); };
  const document = {createElement:tag => new Element(tag)};
  const blobs = new Map();
  let invalidations = 0, nextUrl = 0;
  const compilations = [], ticks = [], scenes = [], rawFrames = [], module = {};
  const retained = {headerSvg,
    PreviewDisplay:class {
      constructor(compiled) { assert.equal(compiled, module); }
      tick(input, elapsed, serial, track, reduced, activity) {
        ticks.push({input, elapsed, serial, track, reduced, activity});
        const frame = Int32Array.of(17); rawFrames.push(frame); return frame;
      }
    },
    SvgDisplay:class {
      constructor(artwork) { this.artwork = artwork; scenes.push(this); }
      header = ['', 'WIDE', '', ''];
      apply(frame) { this.frame = frame; }
      svg() { return '<g transform="translate(80 60)">' + this.artwork + '<text>Default frame ' + this.frame[0] + '</text></g>'; }
    },
  };
  const config = createGraphicsConfiguration();
  let currentMachines = machines;
  const snapshots = createSnapshotService({...retained, compile:bytes => {
    assert.deepEqual(bytes, wasm);
    return new Promise((resolve, reject) => {
      compilations.push({finish:() => resolve(module), reject});
      if (!delayed) resolve(module);
    });
  }});
  const dialog = createGraphicsDialog({elements:Object.fromEntries(elements), configuration:config, snapshots, document,
    onChange() { invalidations++; }, urlApi:{
      createObjectURL(blob) { const url = 'blob:preview-' + ++nextUrl; blobs.set(url, blob); return url; },
      revokeObjectURL(url) { blobs.delete(url); },
    }});
  return {
    $, config, snapshots, dialog, machines:() => currentMachines, selectMachines:value => { currentMachines = value; },
    blobs, events:{pagehide:dialog.clear}, compilations, ticks, scenes, rawFrames, invalidations:() => invalidations,
    artwork:index => blobs.get($('selected-previews-list').children[index].children[1].children[0].src).text(),
    figures:() => $('selected-previews-list').children,
    open:() => dialog.open({machines:currentMachines, screenTemplate}), close:() => $('close-selected-previews').onclick(),
    override(index, value) {
      const select = $('selected-previews-list').children[index].children[3].children[1];
      select.value = value; select.onchange();
    },
    defaultGraphics(value) { if (config.setAll(currentMachines, value)) invalidations++; },
    all(value) { $(value === 'grayscale' ? 'graphics-all-grayscale' : 'graphics-all-original').onclick(); },
  };
}

async function until(predicate) {
  const deadline = Date.now() + 2000;
  while (!predicate()) {
    assert.ok(Date.now() < deadline, 'Snapshot did not reach the expected asynchronous state');
    await new Promise(resolve => setTimeout(resolve, 1));
  }
}

test('configuration instances are isolated and exported settings cannot mutate stored choices', () => {
  const config = createGraphicsConfiguration(), other = createGraphicsConfiguration();
  const second = {...machine, id:'second'}, stock = {id:'stock'};
  assert.equal(config.setMachine(machine, 'invalid'), false);
  assert.equal(config.setMachine(stock, 'grayscale'), false);
  assert.equal(config.setMachine(machine, 'grayscale'), true);
  assert.equal(config.setMachine(second, 'grayscale'), true);
  const settings = config.settings([machine, stock]);
  assert.deepEqual(settings, {graphics:'colorful', graphicsOverrides:{animated:'grayscale'}});
  settings.graphicsOverrides.animated = 'colorful';
  assert.equal(config.modeFor(machine), 'grayscale');
  assert.deepEqual(other.summary([machine]), {enabled:false, customized:0});
  assert.equal(config.setAll([machine], 'colorful'), true);
  assert.equal(config.modeFor(machine), 'colorful');
  assert.equal(config.modeFor(second), 'grayscale');
});

test('default UI is rendered once with default controls, frozen, and reused across comparisons and reopen', async () => {
  const h = await harness();
  h.defaultGraphics('grayscale');
  h.open(); await h.dialog.settled();
  assert.equal(h.figures()[0].children[1].children[0].tag, 'img');
  const gray = await h.artwork(0);
  assert.match(gray, /translate\(80 60\)/);
  assert.match(gray, /Default frame 17/);
  assert.ok(gray.includes(grayscale));
  assert.ok(gray.includes('width')); assert.ok(gray.includes('WIDE')); assert.ok(gray.includes('65'));
  assert.equal(h.$('selected-previews-count').textContent, '1 machine selected');
  assert.equal(h.compilations.length, 1); assert.equal(h.ticks.length, 1);
  assert.deepEqual(h.ticks[0], {input:shownControls([...machine.preview.defaults, 0, 0, 0, 0], [0, 1, 2, 3]),
    elapsed:0, serial:0, track:0, reduced:true, activity:{held_notes:0, note_off_serial:0}});
  h.rawFrames[0][0] = 99; // A snapshot must not retain the WASM memory view.
  h.all('colorful'); await h.dialog.settled();
  assert.ok((await h.artwork(0)).includes(original));
  assert.match(await h.artwork(0), /Default frame 17/);
  assert.equal(h.scenes.length, 2);
  assert.equal(h.scenes[0].frame, h.scenes[1].frame);
  h.all('grayscale'); await h.dialog.settled();
  assert.equal(await h.artwork(0), gray);
  assert.equal(h.blobs.size, 1);
  assert.equal(h.scenes.length, 2); // Both SVG variants are cached.
  assert.equal(h.config.settings([]).graphics, 'grayscale'); assert.equal(h.invalidations(), 3);
  h.close(); assert.equal(h.blobs.size, 0); assert.equal(h.figures().length, 0);
  assert.equal(h.$('graphics-all-original').onclick, null);
  h.open(); await h.dialog.settled();
  assert.equal(await h.artwork(0), gray);
  assert.equal(h.compilations.length, 1); assert.equal(h.ticks.length, 1); assert.equal(h.scenes.length, 2);
  h.events.pagehide(); assert.equal(h.blobs.size, 0); assert.equal(h.figures().length, 0);
});

test('bulk install actions reset selected exceptions, retain unselected exceptions, and reuse snapshots', async () => {
  const machines = [machine, {...machine, id:'second', name:'Second'}, {id:'stock', name:'Stock'}];
  const h = await harness({machines}); h.defaultGraphics('grayscale'); h.open();
  const control = index => h.figures()[index].children[3].children[1];
  h.override(0, 'colorful'); h.override(1, 'colorful'); await h.dialog.settled();
  h.close(); h.selectMachines(machines.slice(1)); h.open();
  const changes = h.invalidations();
  h.all('grayscale'); await h.dialog.settled();
  assert.equal(h.invalidations(), changes + 1); // One batched invalidation.
  assert.equal(control(0).value, 'grayscale');
  assert.deepEqual(control(0).children.map(option => option.textContent), ['Enforce grayscale', 'Keep original design']);
  assert.equal(h.config.summary(h.machines()).enabled, true);
  assert.ok((await h.artwork(0)).includes(grayscale));
  assert.equal(h.figures()[1].children.length, 3);
  h.close(); h.selectMachines(machines); h.open(); await h.dialog.settled();
  assert.equal(control(0).value, 'colorful'); // The unselected exception survived.
  assert.equal(control(1).value, 'grayscale');
  assert.ok((await h.artwork(0)).includes(original));
  assert.ok((await h.artwork(1)).includes(grayscale));
  h.all('colorful'); await h.dialog.settled();
  assert.equal(h.config.settings([]).graphics, 'colorful');
  assert.equal(h.config.summary(h.machines()).enabled, false);
  for (let i = 0; i < 2; i++) {
    assert.equal(control(i).value, 'colorful');
    assert.equal(control(i).children.length, 2);
    assert.ok((await h.artwork(i)).includes(original));
  }
  assert.equal(h.config.summary(h.machines()).customized, 0);
  h.all('grayscale'); await h.dialog.settled();
  h.override(0, 'colorful'); await h.dialog.settled();
  assert.ok((await h.artwork(0)).includes(original));
  const beforeReset = h.invalidations();
  h.all('grayscale'); await h.dialog.settled();
  assert.equal(h.invalidations(), beforeReset + 1);
  assert.ok((await h.artwork(0)).includes(grayscale));
  const beforeRepeat = h.invalidations();
  h.all('grayscale'); await h.dialog.settled();
  assert.equal(h.invalidations(), beforeRepeat); // An identical action is a no-op.
  assert.equal(h.ticks.length, 2);
  h.close();
});

test('latest bulk action wins while rendering and closed dialogs receive no late images', async () => {
  const h = await harness({delayed:true});
  h.open(); await until(() => h.compilations.length === 1);
  h.all('colorful'); h.close();
  h.compilations[0].finish(); await h.dialog.settled();
  assert.equal(h.blobs.size, 0); assert.equal(h.figures().length, 0);
  h.open(); h.all('colorful'); await h.dialog.settled();
  assert.ok((await h.artwork(0)).includes(original));
  assert.equal(h.compilations.length, 1); assert.equal(h.ticks.length, 1);
  h.all('grayscale'); h.all('colorful'); await h.dialog.settled();
  assert.ok((await h.artwork(0)).includes(original));
  assert.equal(h.blobs.size, 1); h.close();
});

test('disposing a dialog removes its handlers and blocks late snapshot results', async () => {
  const h = await harness({delayed:true});
  h.open(); await until(() => h.compilations.length === 1);
  h.dialog.dispose();
  assert.equal(h.$('selected-previews').listeners.close, undefined);
  assert.equal(h.$('close-selected-previews').onclick, null);
  assert.equal(h.$('graphics-all-original').onclick, null);
  h.compilations[0].finish(); await h.dialog.settled();
  assert.equal(h.blobs.size, 0);
  assert.equal(h.figures().length, 0);
});

test('failed modules leave other snapshots usable and missing modules have an explicit static fallback', async () => {
  const h = await harness({machines:[{...machine, id:'bad', preview:{...machine.preview, sha256:'invalid'}},
    machine, {...machine, id:'static', preview:undefined}]});
  h.open(); await h.dialog.settled();
  assert.match(h.figures()[0].children[1].children[0].textContent, /Preview unavailable: Preview module hash mismatch/);
  assert.match(h.figures()[2].children[2].textContent, /Static artwork; rendered snapshot unavailable/);
  assert.equal(await h.artwork(2), screenTemplate.replace('</svg>', original + '</svg>'));
  assert.ok((await h.artwork(1)).includes('Default frame 17'));
  h.override(1, 'colorful'); await h.dialog.settled();
  assert.ok((await h.artwork(1)).includes(original));
  assert.equal(h.ticks.length, 1); h.close(); assert.equal(h.blobs.size, 0);
});

test('an invalid artwork does not hide another machine’s rendered snapshot', async () => {
  const bad = {...machine, id:'bad', artwork:original.replaceAll('#ff3300', 'url(#missing)'), grayscaleArtwork:undefined};
  const h = await harness({machines:[bad, machine]});
  h.defaultGraphics('grayscale');
  h.open(); await h.dialog.settled();
  assert.match(h.figures()[0].children[1].children[0].textContent, /Preview unavailable/);
  assert.ok((await h.artwork(1)).includes('Default frame 17'));
  h.close(); assert.equal(h.blobs.size, 0);
});

test('enabling grayscale defaults every selected custom screen to grayscale before configuring exceptions', async () => {
  const machines = [machine, {...machine, id:'automatic', name:'Automatic', grayscaleArtwork:undefined},
    {...machine, id:'neutral', name:'Neutral', artwork:grayscale, grayscaleArtwork:undefined}, {id:'stock', name:'Stock'}];
  const h = await harness({machines});
  h.open(); await h.dialog.settled();
  const control = index => h.figures()[index].children[3].children[1];
  for (let index = 0; index < 3; index++) {
    assert.equal(control(index).value, 'colorful');
    assert.deepEqual(control(index).children.map(option => option.value), ['grayscale', 'colorful']);
  }
  h.close(); h.defaultGraphics('grayscale'); h.open(); await h.dialog.settled();
  assert.equal(h.$('graphics-all-grayscale').disabled, false);
  for (let index = 0; index < 3; index++) {
    assert.equal(control(index).value, 'grayscale');
    assert.equal(control(index).children.length, 2);
    assert.ok((await h.artwork(index)).includes(Core.selectArtwork(machines[index], 'grayscale')));
  }
  assert.match(h.figures()[0].children[2].textContent, /provided by the developer/);
  assert.match(h.figures()[1].children[2].textContent, /Automatic grayscale/);
  assert.match(h.figures()[2].children[2].textContent, /Grayscale by design.*kept intact/);
  assert.equal(h.figures()[3].children.length, 3);
  assert.match(h.figures()[3].children[2].textContent, /Stock screen.*unchanged/);
  h.override(1, 'colorful'); await h.dialog.settled();
  assert.equal(h.config.settings([]).graphics, 'grayscale');
  assert.ok((await h.artwork(1)).includes(original));
  assert.ok((await h.artwork(0)).includes(grayscale));
  h.close();
});

test('configuration derives enabled state from selected machines and applies bulk changes atomically', async () => {
  const machines = [machine, {...machine, id:'second', name:'Second'}, {id:'stock', name:'Stock'}];
  const h = await harness({machines});
  const enabled = () => h.config.summary(h.machines()).enabled;
  const summary = () => h.config.summary(h.machines());
  h.open(); await h.dialog.settled();
  assert.equal(enabled(), false);
  assert.equal(h.invalidations(), 0); // Opening does not enable or modify the build.
  h.override(0, 'grayscale'); await h.dialog.settled();
  assert.equal(enabled(), true);
  assert.deepEqual(summary(), {enabled:true, customized:1});
  h.override(1, 'grayscale'); h.override(0, 'colorful');
  assert.equal(enabled(), true); // The other selected machine still requests grayscale.
  h.override(1, 'colorful'); await h.dialog.settled();
  assert.equal(enabled(), false);
  assert.deepEqual(summary(), {enabled:false, customized:0});
  h.override(0, 'grayscale'); h.close();
  assert.equal(enabled(), true); // Closing preserves the configuration.
  h.selectMachines(machines.slice(1));
  assert.equal(enabled(), false); // An unselected machine's override is not an enabled extension.
  h.defaultGraphics('grayscale'); h.open(); await h.dialog.settled();
  h.override(0, 'colorful'); await h.dialog.settled();
  assert.equal(enabled(), false); // All originals also turn it off when the global default is grayscale.
  assert.equal(h.config.settings([]).graphics, 'grayscale');
  h.close(); h.defaultGraphics('grayscale'); // Checking it again clears the selected original exception.
  assert.equal(enabled(), true);
  assert.deepEqual(summary(), {enabled:true, customized:0});
  h.defaultGraphics('colorful');
  assert.equal(enabled(), false);
  h.selectMachines(machines);
  assert.equal(enabled(), true); // The unselected grayscale exception survived both checkbox actions.
  assert.equal(h.config.settings([]).graphics, 'colorful');
  const beforeDisable = h.invalidations();
  h.defaultGraphics('colorful');
  assert.equal(enabled(), false); // Unchecking clears grayscale exceptions even with an original default.
  assert.equal(h.invalidations(), beforeDisable + 1);
  assert.deepEqual(summary(), {enabled:false, customized:0});
  h.defaultGraphics('colorful');
  assert.equal(h.invalidations(), beforeDisable + 1); // No change means no new invalidation.
  h.open(); await h.dialog.settled();
  assert.ok((await h.artwork(0)).includes(original));
  assert.ok((await h.artwork(1)).includes(original));
  h.close();
});

test('stock-only selections disable bulk actions and keep the installation default unchanged', async () => {
  const h = await harness({machines:[{id:'stock', name:'Stock'}]});
  h.open();
  assert.equal(h.$('graphics-all-grayscale').disabled, true);
  assert.equal(h.$('graphics-all-original').disabled, true);
  h.all('grayscale');
  assert.equal(h.config.settings([]).graphics, 'colorful');
  assert.equal(h.invalidations(), 0);
  h.close();
  h.defaultGraphics('grayscale');
  assert.equal(h.config.summary(h.machines()).enabled, false);
  assert.equal(h.config.settings([]).graphics, 'colorful');
  assert.equal(h.invalidations(), 0);
});
