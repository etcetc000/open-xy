import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {artworkForGraphics, buildAssets, sha256, zipFiles} from '../builder/core.mjs';
import * as Core from '../builder/core.mjs';
import {createBuilder} from '../builder/app.mjs';
import {selectArtwork, validateGrayscaleArtwork} from '../sdk/browser/graphics.mjs';
import {graphicsExampleArtwork} from '../builder/visuals.mjs';

// Authored artwork: visible paint, dynamic text and off-screen animation donors.
// Text and comments deliberately contain colour-looking strings that are not paint.
const artwork = `<g id="probe_panel">
  <!-- fill="#ff0000" stays a comment -->
  <path id="probe_node_0" d="M0 0 L20 0 L20 20 Z" fill="#FF0000" stroke='#00ff00' stroke-width="2" transform="matrix(1 0 0 1 20 40)" fill-opacity="0.5"/>
  <circle id="probe_node_1" cx="0" cy="0" r="4" fill="none" stroke="#0000ff" stroke-width="1"/>
  <text id="probe_text_0" font-family="OP-XY" font-size="12" fill="#ffff00">stroke='#ff0000'</text>
  <circle id="probe_palette_0" cx="640" cy="0" r="1" fill="#ff0000" stroke="#00ffff" stroke-width="1"/>
  <circle id="probe_palette_1" cx="640" cy="0" r="1" fill="#00ff00" stroke="#ff00ff" stroke-width="2"/>
  <circle id="probe_palette_2" cx="640" cy="0" r="1" fill="#0000ff" stroke="#ffff00" stroke-width="3"/>
</g>`;
const paintValues = svg => [...svg.replace(/<!--[\s\S]*?-->|<text\b[^>]*>[\s\S]*?<\/text>/g,
  match => match.startsWith('<text') ? match.slice(0, match.indexOf('>') + 1) : '')
  .matchAll(/\b(?:fill|stroke)=(['"])(.*?)\1/g)].map(match => match[2]);
const isGray = value => value === 'none' || /^#([a-f0-9]{2})\1\1$/i.test(value);
const svgAttributes = (source, id) => Object.fromEntries([...source.match(new RegExp(`<\\w+ id="${id}"([^>]*)>`))[1]
  .matchAll(/([\w-]+)="([^"]*)"/g)].map(([, key, value]) => [key, value]));
const luminance = hex => hex.slice(1).match(/../g).map(part => parseInt(part, 16) / 255)
  .map(value => value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4)
  .reduce((sum, value, i) => sum + value * [.2126, .7152, .0722][i], 0);
const contrast = (a, b) => (Math.max(luminance(a), luminance(b)) + .05) /
  (Math.min(luminance(a), luminance(b)) + .05);

test('comparison examples demonstrate intact grayscale artwork and real colorful conversion', () => {
  const examples = graphicsExampleArtwork();
  assert.equal(examples.length, 2);
  assert.equal(examples[0].name, 'Waveform');
  assert.ok(paintValues(examples[0].artwork).every(isGray));
  assert.equal(artworkForGraphics(examples[0].artwork, 'grayscale'), examples[0].artwork);
  assert.ok(paintValues(examples[1].artwork).some(value => !isGray(value)));
  assert.notEqual(artworkForGraphics(examples[1].artwork, 'grayscale'), examples[1].artwork);
  for (const {artwork: original} of examples) {
    const converted = artworkForGraphics(original, 'grayscale');
    assert.ok(paintValues(converted).every(isGray));
    const omitPaint = source => source.replace(/\b(?:fill|stroke)=(["'])(.*?)\1/g, 'paint');
    assert.equal(omitPaint(converted), omitPaint(original));
    assert.equal(artworkForGraphics(original, 'colorful'), original);
  }
});

test('grayscale Waveform keeps consistent labeled tone roles with clear trace and meter contrast', () => {
  const original = graphicsExampleArtwork()[0].artwork;
  assert.equal(artworkForGraphics(original, 'grayscale'), original);
  assert.equal(artworkForGraphics(original, 'colorful'), original);
  const background = svgAttributes(original, 'waveform_background').fill;
  const grid = svgAttributes(original, 'waveform_grid_0').stroke;
  assert.equal(background, '#000000');
  const tones = ['a', 'b', 'mix'].map(role => svgAttributes(original, 'waveform_meter_' + role).fill);
  assert.equal(new Set(tones).size, 3);
  for (const [i, role] of ['a', 'b', 'mix'].entries()) {
    assert.ok(contrast(tones[i], background) >= 3, role + ' meter');
    if (role !== 'mix') {
      const trace = svgAttributes(original, 'waveform_osc_' + role);
      assert.equal(trace.stroke, tones[i]);
      assert.ok(contrast(trace.stroke, grid) >= 3, role + ' trace over grid');
      assert.ok(contrast(trace.stroke, background) >= 3, role + ' trace over background');
    }
    const labelFills = [...original.matchAll(new RegExp(`<text([^>]*)>${role.toUpperCase()}</text>`, 'g'))]
      .map(([, attributes]) => attributes.match(/fill="([^"]+)"/)[1]);
    assert.equal(labelFills.length, role === 'mix' ? 1 : 2); // Direct trace and meter labels.
    labelFills.forEach(fill => {
      assert.equal(fill, tones[i]);
      assert.ok(contrast(fill, background) >= 4.5, role + ' label');
    });
  }
  assert.ok(Math.abs(parseInt(tones[0].slice(1, 3), 16) - parseInt(tones[1].slice(1, 3), 16)) >= 60);
});

test('step sequence uses four clear hue roles with readable labels and a color-independent selection', () => {
  const original = graphicsExampleArtwork()[1].artwork;
  const grayscale = artworkForGraphics(original, 'grayscale');
  const rect = svgAttributes;
  assert.equal(new Set(paintValues(original).filter(value => !isGray(value))).size, 4);
  for (const source of [original, grayscale]) {
    const colors = [];
    for (let i = 1; i <= 16; i++) {
      const bar = rect(source, 'sequence_step_' + i), track = rect(source, 'sequence_track_' + i);
      colors.push(bar.fill);
      assert.ok(contrast(bar.fill, track.fill) >= 3, `Step ${i} contrast`);
      assert.equal(bar.fill, colors[Math.floor((i - 1) / 4) * 4]);
    }
    assert.equal(new Set(colors).size, 4);
    for (const label of ['GROUP A', 'GROUP B', 'GROUP C', 'GROUP D']) {
      const attributes = source.match(new RegExp(`<text([^>]*)>${label}</text>`))[1];
      const fill = attributes.match(/fill="([^"]+)"/)[1];
      assert.ok(contrast(fill, '#000000') >= 4.5, label);
    }
    const selection = rect(source, 'sequence_current_step'), selected = rect(source, 'sequence_step_4');
    assert.equal(selection.fill, 'none');
    assert.equal(selection.stroke, '#ffffff');
    for (const axis of ['x', 'y']) {
      const size = axis === 'x' ? 'width' : 'height';
      assert.ok(Number(selected[axis]) - Number(selection[axis]) >= 3);
      assert.ok(Number(selection[axis]) + Number(selection[size]) - Number(selected[axis]) - Number(selected[size]) >= 3);
    }
  }
  const grays = [1, 5, 9, 13].map(i => parseInt(rect(grayscale, 'sequence_step_' + i).fill.slice(1, 3), 16));
  assert.ok(grays.slice(1).every((level, i) => level - grays[i] >= 45));
  const originalBrightness = [1, 5, 9, 13].map(i => {
    const fill = rect(original, 'sequence_step_' + i).fill;
    return parseInt(paintValues(artworkForGraphics(`<circle fill="${fill}"/>`, 'grayscale'))[0].slice(1, 3), 16);
  });
  assert.ok(originalBrightness.slice(1).every((level, i) => level - originalBrightness[i] >= 20));
});

test('explicit colorful and legacy asset calls preserve original SVG bytes exactly', () => {
  assert.equal(artworkForGraphics(artwork), artwork);
  assert.equal(artworkForGraphics(artwork, 'colorful'), artwork);
});

test('developer artwork takes precedence over conversion and preserves original no-op behavior', async () => {
  const variant = artworkForGraphics(artwork, 'grayscale').replace(/#([a-f0-9]{2})\1\1/gi, '#bbbbbb');
  const bindings = {nodes:['probe_node_0','probe_node_1'], texts:['probe_text_0'],
    palette:['probe_palette_0','probe_palette_1','probe_palette_2'], paths:0, points:0};
  const machine = {artwork, grayscaleArtwork:variant, preview:{graphics:bindings}};
  assert.equal(selectArtwork(machine, 'colorful'), artwork);
  assert.equal(selectArtwork(machine, 'grayscale'), variant);
  assert.equal(selectArtwork({...machine, artwork:artwork.replace('#FF0000', 'orange')}, 'grayscale'), variant);
  assert.equal(selectArtwork({...machine, artwork:variant}, 'grayscale'), variant);
  assert.equal(selectArtwork({artwork}, 'grayscale'), artworkForGraphics(artwork, 'grayscale'));
  for (const invalid of [variant.replace('probe_node_0', 'missing_node_0'),
    variant.replace('fill="#bbbbbb"', 'fill="#ff0000"'),
    variant.replace('matrix(1 0 0 1 20 40)', 'matrix(1 0 0 1 21 40)'),
    variant.replace('</g>', '<circle fill="#ff0000"/></g>'),
    variant.replace('stroke-width="2"', 'stroke-width="2.00001"')]) {
    assert.throws(() => validateGrayscaleArtwork(artwork, invalid, bindings));
    assert.throws(() => selectArtwork({...machine, grayscaleArtwork:invalid}, 'grayscale'));
  }
  const {kit, catalog} = await fixture();
  Object.assign(kit.machines[0], machine);
  const files = await buildAssets(kit, ['probe'], catalog, {}, {graphics:'grayscale'});
  assert.equal(new TextDecoder().decode(files['content/display/probe.svg']), kit.screenTemplate.replace('</svg>', variant + '</svg>'));
});

test('grayscale covers initial paint, text and every animation palette donor', () => {
  const gray = artworkForGraphics(artwork, 'grayscale');
  assert.ok(paintValues(gray).every(isGray));
  const originalPaint = paintValues(artwork), grayPaint = paintValues(gray);
  const mapping = new Map(originalPaint.map((value, i) => [value.toLowerCase(), grayPaint[i]]));
  const channels = ['#ff0000', '#00ff00', '#0000ff'].map(color => parseInt(mapping.get(color).slice(1, 3), 16));
  assert.ok(Math.min(...channels) >= 96);
  assert.ok(Math.max(...channels) - Math.min(...channels) >= 60);
  // Repeated colors use one mapping across static paint and animation donors.
  originalPaint.forEach((value, i) => assert.equal(grayPaint[i], mapping.get(value.toLowerCase())));
  assert.match(gray, /fill-opacity="0.5"/);
  assert.match(gray, /<!-- fill="#ff0000" stays a comment -->/);
  assert.match(gray, />stroke='#ff0000'<\/text>/);
  const withoutPaint = svg => svg.replace(/<(?:path|circle|text)\b[^>]*>/g,
    tag => tag.replace(/\b(?:fill|stroke)=(['"])(.*?)\1/g, 'paint'));
  assert.equal(withoutPaint(gray), withoutPaint(artwork));
  assert.equal(artworkForGraphics(gray, 'grayscale'), gray);
  // Restoring colourful derives from the original, never the converted copy.
  assert.equal(artworkForGraphics(artwork, 'colorful'), artwork);
});

test('hue distinctions survive equal luminance and take priority over source brightness', () => {
  const source = '<g><circle fill="#ff0000"/><circle fill="#009400"/><circle fill="#990000"/></g>';
  // Red and this green both become approximately #7f7f7f under luminance-only
  // conversion. A darker red stays in the red family instead of crossing green.
  const [red, green, darkRed] = paintValues(artworkForGraphics(source, 'grayscale'))
    .map(value => parseInt(value.slice(1, 3), 16));
  assert.ok(Math.abs(red - green) >= 140);
  assert.ok(Math.abs(red - darkRed) <= 16);
  assert.ok(darkRed < red);
  assert.ok(Math.abs(darkRed - green) >= 140);
});

test('sparse palettes get wider hue separation while dense palettes share the available range', () => {
  const palettes = [
    ['#ff0000', '#00ffff'],
    ['#ff0000', '#ffff00', '#00ff00', '#0000ff'],
    ['#ff0000', '#ffbf00', '#80ff00', '#00ff40', '#00ffff', '#0040ff', '#8000ff', '#ff00bf'],
  ];
  const gaps = palettes.map(colors => {
    const source = '<g>' + colors.map(fill => `<circle fill="${fill}"/>`).join('') + '</g>';
    const levels = paintValues(artworkForGraphics(source, 'grayscale'))
      .map(value => parseInt(value.slice(1, 3), 16)).sort((a, b) => a - b);
    assert.equal(new Set(levels).size, colors.length);
    assert.ok(levels[0] >= 96); // The darkest chromatic role stays visible on black.
    assert.ok(levels.at(-1) <= 248);
    return Math.min(...levels.slice(1).map((value, i) => value - levels[i]));
  });
  assert.ok(gaps[0] >= 140);
  assert.ok(gaps[1] >= 45);
  assert.ok(gaps[2] >= 18);
  assert.ok(gaps[0] > gaps[1] && gaps[1] > gaps[2]);
});

test('small palettes distinguish closer hues and reserve shading only where it is needed', () => {
  const levels = colors => paintValues(artworkForGraphics('<g>' +
    colors.map(fill => `<circle fill="${fill}"/>`).join('') + '</g>', 'grayscale'))
    .map(value => parseInt(value.slice(1, 3), 16));
  const sparse = levels(['#ff0000', '#ff4000', '#0000ff']);
  const dense = levels(['#ff0000', '#ff4000', '#0000ff', '#ffff00', '#80ff00', '#00ff00',
    '#00ff80', '#00ffff', '#0080ff', '#8000ff', '#ff00ff']);
  assert.ok(Math.abs(sparse[0] - sparse[1]) >= 60);
  assert.ok(Math.abs(sparse[0] - sparse[1]) > Math.abs(dense[0] - dense[1]));
  const shaded = levels(['#ff0000', '#660000', '#990000', '#bb0000', '#00ff00', '#00aa00', '#0000ff']);
  const red = shaded.slice(0, 4), green = shaded.slice(4, 6), blue = shaded[6];
  const shadeSpan = Math.max(Math.max(...red) - Math.min(...red), Math.abs(green[0] - green[1]));
  const hueGap = Math.min(...red.flatMap(r => green.map(g => Math.abs(r - g))),
    ...red.map(r => Math.abs(r - blue)), ...green.map(g => Math.abs(g - blue)));
  assert.ok(shadeSpan > 0);
  assert.ok(hueGap >= shadeSpan * 2);
});

test('palette mapping handles the hue wrap and is independent of aliases, order and frequency', () => {
  const paints = ['#ff0010', '#ff1000', 'red', '#f00', '#0000ff', '#000000', '#FFF', 'gray'];
  const convert = values => {
    const source = '<g>' + values.map(fill => `<circle fill="${fill}"/>`).join('') + '</g>';
    return new Map(values.map((fill, i) => [fill, paintValues(artworkForGraphics(source, 'grayscale'))[i]]));
  };
  const mapping = convert(paints);
  assert.deepEqual(convert([...paints].reverse()), mapping);
  assert.deepEqual(convert([...paints, ...paints.slice(0, 5)]), mapping);
  assert.equal(mapping.get('red'), mapping.get('#f00'));
  assert.equal(mapping.get('#000000'), '#000000');
  assert.equal(mapping.get('#FFF'), '#FFF');
  assert.equal(mapping.get('gray'), 'gray');
  const channel = color => parseInt(mapping.get(color).slice(1, 3), 16);
  assert.ok(Math.abs(channel('#ff0010') - channel('#ff1000')) <= 4);
  assert.ok(Math.abs(channel('#ff0010') - channel('#0000ff')) >= 80);
});

test('weak tints and a single hue keep brightness without amplifying subdued backgrounds', () => {
  const source = '<g><circle fill="#10151e"/><circle fill="#ff0000"/><circle fill="#990000"/></g>';
  const converted = artworkForGraphics(source, 'grayscale');
  assert.deepEqual(paintValues(converted), ['#151515', '#7f7f7f', '#4a4a4a']);
});

test('existing grays, none, short hex and basic SVG colour names are supported', () => {
  for (const value of ['#000000', '#FFF', '#5a5a5a', 'none', 'black', 'white', 'gray', 'silver']) {
    const source = `<circle fill="${value}"/>`;
    assert.equal(artworkForGraphics(source, 'grayscale'), source);
  }
  for (const value of ['#f00', 'red', '#0F0', 'blue', 'yellow'])
    assert.ok(paintValues(artworkForGraphics(`<circle fill="${value}"/>`, 'grayscale')).every(isGray));
});

test('grayscale fails closed on unknown paints or SVG features that could bypass conversion', () => {
  for (const value of ['currentColor', 'url(#paint)', 'inherit', 'var(--color)', 'rgb(255, 0, 0)',
    '#ff000080', 'ff0000', '&#35;ff0000'])
    assert.throws(() => artworkForGraphics(`<circle fill="${value}"/>`, 'grayscale'), /Unsupported SVG paint/);
  for (const source of ['<g style="fill:red"/>', '<circle color="red"/>', '<image href="data:x"/>',
    '<linearGradient/>', '<use href="#x"/>', '<circle fill="red" filter="url(#f)"/>',
    '<circle fill=red/>', '<!DOCTYPE svg>', '<circle fill="red"/><'])
    assert.throws(() => artworkForGraphics(source, 'grayscale'), /SVG/);
  for (const graphics of ['', null, true, 'grey', 'grayscale '])
    assert.throws(() => artworkForGraphics(artwork, graphics), /Invalid machine graphics setting/);
});

async function fixture() {
  const text = '{"engine_types":[{"type":"stock"}\n],"fx_types":[]}';
  const catalog = new TextEncoder().encode(text);
  const screenTemplate = readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg', import.meta.url), 'utf8');
  const machine = {id:'probe', kind:'synth', name:'Probe', artwork, asset:'probe.svg',
    catalog:',{"type":"probe","default_params":[0,0,0,0,0,0,0,0]}'};
  const kit = {machines:[machine, {...machine, id:'other', asset:'other.svg', artwork:'<circle fill="unsupported"/>'}],
    screenTemplate, profile:{stock_catalog_sha256:await sha256(catalog),
      screen_template_sha256:await sha256(new TextEncoder().encode(screenTemplate)),
      stock_catalog_inserts:{engine_types:text.indexOf('\n]'), fx_types:text.lastIndexOf(']')}}};
  return {kit, catalog};
}

test('per-machine overrides package mixed designs with authored grayscale and intact grayscale originals', async () => {
  const {kit, catalog} = await fixture();
  const first = kit.machines[0];
  const gray = artworkForGraphics(first.artwork, 'grayscale');
  first.grayscaleArtwork = gray.replaceAll('#f8f8f8', '#bbbbbb');
  kit.machines[1] = {...first, id:'other', asset:'other.svg'};
  kit.machines.push({...first, id:'neutral', asset:'neutral.svg', artwork:gray});
  const selection = kit.machines.map(machine => machine.id);
  const original = await buildAssets(kit, selection, catalog, {}, {graphics:'colorful'});
  const mixed = await buildAssets(kit, selection, catalog, {}, {graphics:'grayscale', graphicsOverrides:{probe:'colorful'}});
  const decode = bytes => new TextDecoder().decode(bytes);
  assert.deepEqual(mixed['content/display/probe.svg'], original['content/display/probe.svg']);
  assert.equal(decode(mixed['content/display/other.svg']), kit.screenTemplate.replace('</svg>', kit.machines[1].grayscaleArtwork + '</svg>'));
  assert.deepEqual(mixed['content/display/neutral.svg'], original['content/display/neutral.svg']);
  const enforced = await buildAssets(kit, selection, catalog, {}, {graphics:'colorful', graphicsOverrides:{probe:'grayscale'}});
  assert.notDeepEqual(enforced['content/display/probe.svg'], original['content/display/probe.svg']);
  assert.deepEqual(enforced['content/display/other.svg'], original['content/display/other.svg']);
  delete first.grayscaleArtwork;
  const fallback = await buildAssets(kit, ['probe'], catalog, {}, {graphics:'colorful', graphicsOverrides:{probe:'grayscale'}});
  assert.equal(decode(fallback['content/display/probe.svg']), kit.screenTemplate.replace('</svg>', gray + '</svg>'));
  const unselected = await buildAssets(kit, ['probe'], catalog, {}, {graphics:'colorful', graphicsOverrides:{other:'grayscale'}});
  assert.deepEqual(unselected['content/display/probe.svg'], original['content/display/probe.svg']);
  assert.equal(Object.hasOwn(unselected, 'content/display/other.svg'), false);
  for (const path of Object.keys(original).filter(path => !path.startsWith('content/display/')))
    assert.deepEqual(mixed[path], original[path]);
});

test('invalid per-machine policies fail before packaging and stock screens cannot be overridden', async () => {
  const {kit, catalog} = await fixture();
  kit.machines.push({id:'stock', kind:'effect'});
  for (const graphicsOverrides of [null, [], true, {unknown:'grayscale'}, {probe:'grey'}, {probe:'default'}, {stock:'grayscale'}])
    await assert.rejects(buildAssets(kit, ['probe'], catalog, {}, {graphicsOverrides}), /graphics override/i);
  assert.equal(Core.graphicsForMachine(kit.machines[0], {graphics:'grayscale', graphicsOverrides:Object.create({probe:'colorful'})}), 'grayscale');
});

test('packaging converts only selected screens and preserves the template, menu, presets and source', async () => {
  const {kit, catalog} = await fixture(), original = structuredClone(kit);
  const colorful = await buildAssets(kit, ['probe'], catalog, {});
  assert.deepEqual(colorful, await buildAssets(kit, ['probe'], catalog, {}, {graphics:'colorful'}));
  const grayscale = await buildAssets(kit, ['probe'], catalog, {}, {graphics:'grayscale'});
  assert.deepEqual(Object.keys(grayscale), Object.keys(colorful));
  const path = 'content/display/probe.svg', decode = bytes => new TextDecoder().decode(bytes);
  assert.equal(decode(colorful[path]), kit.screenTemplate.replace('</svg>', artwork + '</svg>'));
  assert.ok(paintValues(decode(grayscale[path])).every(isGray));
  assert.equal(decode(grayscale[path]).split('<g id="probe_panel">')[0], kit.screenTemplate.split('</svg>')[0]);
  for (const name of Object.keys(colorful).filter(name => name !== path))
    assert.deepEqual(grayscale[name], colorful[name], name);
  assert.deepEqual(kit, original);
  assert.notEqual(await sha256(grayscale[path]), await sha256(colorful[path]));
  // The ZIP's stored bytes are exactly the file map the direct-transfer API uses.
  const zip = zipFiles(grayscale), view = new DataView(zip.buffer);
  for (let offset = 0; view.getUint32(offset, true) === 0x04034b50;) {
    const size = view.getUint32(offset + 18, true), length = view.getUint16(offset + 26, true);
    const name = decode(zip.subarray(offset + 30, offset + 30 + length));
    const start = offset + 30 + length;
    assert.deepEqual(zip.subarray(start, start + size), grayscale[name]);
    offset = start + size;
  }
  await assert.rejects(buildAssets(kit, ['probe', 'other'], catalog, {}, {graphics:'grayscale'}), /other graphics.*Unsupported SVG paint/);
  await assert.rejects(buildAssets(kit, [], catalog, {}, {graphics:'grey'}), /Invalid machine graphics setting/);
  kit.screenTemplate += ' ';
  await assert.rejects(buildAssets(kit, ['probe'], catalog, {}, {graphics:'grayscale'}), /Platform screen template differs/);
});

test('already-grayscale machine screens retain their exact packaged bytes and hashes', async () => {
  const {kit, catalog} = await fixture();
  // Deliberately retain CRLFs, mixed-case hex, short hex, named grays, spacing,
  // quotes and text that looks colourful. None requires a paint conversion.
  kit.machines[0].artwork = `\r\n<g  id = 'probe_panel' >\r\n  <!-- red #ff0000 is documentation -->\r\n  <path id="probe_node_0" d="M0 0 L20 0 L20 20 Z" fill = '#aAaAaA' stroke="#FFF" stroke-width="2" />\r\n  <text id="probe_text_0" fill="white" font-family="OP-XY" font-size="12">red #ff0000</text>\r\n  <circle id="probe_palette_0" cx="640" cy="0" r="1" fill="#5A5A5A" stroke="#808080" stroke-width="1"/>\r\n</g >\r\n`;
  const original = kit.machines[0].artwork;
  assert.equal(artworkForGraphics(original, 'grayscale'), original);
  const colorful = await buildAssets(kit, ['probe'], catalog, {}, {graphics:'colorful'});
  const grayscale = await buildAssets(kit, ['probe'], catalog, {}, {graphics:'grayscale'});
  assert.deepEqual(grayscale, colorful);
  const path = 'content/display/probe.svg';
  assert.equal(await sha256(grayscale[path]), await sha256(colorful[path]));
  assert.equal(kit.machines[0].artwork, original);
  // A grayscale initial pose with a coloured animation donor still needs conversion.
  kit.machines[0].artwork = original.replace('fill="#5A5A5A"', 'fill="#ff0000"');
  const animated = await buildAssets(kit, ['probe'], catalog, {}, {graphics:'grayscale'});
  assert.notDeepEqual(animated[path], grayscale[path]);
  assert.match(new TextDecoder().decode(animated[path]), /fill="#7f7f7f" stroke="#808080"/);
});

test('Extensions graphics defaults to original designs without firmware extensions, rebuilds downloads and rejects results made stale by a setting change', async t => {
  class Element {
    hidden = false; disabled = false; textContent = ''; dataset = {}; children = [];
    listeners = {};
    setAttribute(key, value) { this[key] = value; }
    removeAttribute(key) { delete this[key]; }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = children; }
    addEventListener(type, handler) { this.listeners[type] = handler; }
    showModal() { this.open = true; }
    close() { this.open = false; this.listeners.close?.(); }
    focus() {}
    style = {setProperty() {}};
    classList = {add() {}, toggle() {}};
    get parentElement() { return new Element(); }
    cloneNode() { return new Element(); }
    querySelector() { return new Element(); }
    querySelectorAll() { return []; }
    closest() { return new Element(); }
    reportValidity() { return true; }
  }
  const html = readFileSync(new URL('../builder/index.html', import.meta.url), 'utf8');
  const nav = html.match(/<nav class="steps"[\s\S]*?<\/nav>/)[0];
  assert.deepEqual([...nav.matchAll(/data-step="(\d)"/g)].map(m => Number(m[1])), [1, 2, 3, 4]);
  assert.ok(!html.includes('id="settings-title"'));
  const extensionsPage = html.slice(html.indexOf('<section data-page="3"'), html.indexOf('<section data-page="4"'));
  assert.ok(extensionsPage.includes('id="grayscale-graphics"'));
  assert.match(extensionsPage, /id="preview-selected"[^>]*>Configure<\/button>/);
  assert.doesNotMatch(html, /<input\b[^>]*id="grayscale-graphics"[^>]*\bchecked\b/);
  assert.doesNotMatch(html, /<input\b[^>]*value="colorful"[^>]*\bchecked\b/);
  const elements = new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(([, id]) => [id, new Element()]));
  const $ = id => { assert.ok(elements.has(id), id); return elements.get(id); };
  const graphicsInput = $('grayscale-graphics');
  const steps = [1, 2, 3, 4].map(step => Object.assign(new Element(), {dataset:{step:String(step)}}));
  const pages = steps.map(el => Object.assign(new Element(), {dataset:{page:el.dataset.step}}));
  const titles = ['input-title', 'machines-title', 'extensions-title', 'download-title'];
  const location = {hash:'#preview/settings', pathname:'/', search:''};
  const globals = {
    document:{getElementById:$, addEventListener() {}, createElement:() => new Element(),
      querySelectorAll:selector => ({'[data-page]':pages, '.steps button':steps, '[data-step]':steps,
        'input[name="graphics"]':[]}[selector] ?? []),
      querySelector:selector => {
        const step = /data-step="(\d)"/.exec(selector);
        if (step) return steps[Number(step[1]) - 1];
        if (selector === '.memory-bar') return new Element();
        const page = /data-page="(\d)"/.exec(selector);
        assert.ok(page, selector);
        return selector.endsWith(' h1') ? $(titles[Number(page[1]) - 1]) : pages[Number(page[1]) - 1];
      }},
    window:{addEventListener() {}}, location,
    history:{replaceState() { location.hash = ''; }},
    localStorage:{setItem() {}},
    matchMedia:() => ({matches:true}), requestAnimationFrame:() => 1, cancelAnimationFrame() {},
  };
  for (const [key, value] of Object.entries(globals)) {
    const previous = Object.getOwnPropertyDescriptor(globalThis, key);
    Object.defineProperty(globalThis, key, {value, configurable:true});
    t.after(() => previous ? Object.defineProperty(globalThis, key, previous) : delete globalThis[key]);
  }
  let wizardOptions, invalidations = 0, finishPatch, blockPatch = false;
  const previewModes = [];
  let currentFixture;
  const fakeCore = {...Core,
    // Keep this test firmware-free: exercise the real app, asset writer and ZIP
    // with an authored kit; OS verification/linking have their own contract tests.
    openKit:async data => ({...data, objects:[], extensions:[],
      machines:data.machines.map(machine => ({controls:[], identity:{}, ...machine}))}),
    chosenByDefault:machine => (currentFixture.selected ?? ['probe']).includes(machine.id),
    decode64:value => value ? Core.decode64(value) : currentFixture.catalog,
    moduleBytes:() => 2048,
    recoverFirmware:async () => ({stock:Uint8Array.of(1)}),
    linkKit:() => ({segments:[], numbers:{}, sizes:{allocated:0, available:2048, initialized:0, working:0}}),
    patchFirmware:async () => blockPatch ? new Promise(resolve => { finishPatch = resolve; }) : Uint8Array.of(1, 2, 3),
  };
  const wizard = {invalidate() { invalidations++; }, navigate() {}, previewLink:() => false,
    clearSourceBackup() {}, setFirmwareState() {}};
  const builder = await createBuilder({core:fakeCore,
    wizardApi:{createWizard:options => { wizardOptions = options; return wizard; }},
    visuals:{reveal() {}, icon:() => new Element(), bundle:() => new Element(),
      graphicsExamples:() => ({elements:[], setGraphics:mode => previewModes.push(mode)})}, supportsBrowser:() => true});
  const api = {async configure(value) {
    currentFixture = value;
    await builder.loadCollection(value.kit);
    $('risk-acknowledgement').checked = true;
    $('risk-form').onsubmit({preventDefault() {}});
    assert.equal(await wizardOptions.onFirmware({name:'Authored test input', size:1,
      arrayBuffer:async () => Uint8Array.of(1).buffer}), true);
  }};
  assert.equal(graphicsInput.checked, false);
  assert.equal($('build').disabled, true); // Preview cannot install.
  const beforeClick = invalidations;
  graphicsInput.onchange();
  assert.equal(previewModes.at(-1), 'colorful');
  assert.equal(invalidations, beforeClick); // Restoring a preview does not rebuild.
  await api.configure(await fixture());
  assert.equal($('preview-selected').disabled, false);
  assert.equal($('extension-count').textContent, 'None selected');
  assert.deepEqual($('build-selection').children.slice(-2).map(e => e.textContent), ['Machine graphics', 'Original designs']);
  await $('build').onclick();
  const defaultFiles = wizardOptions.getFiles();
  assert.deepEqual(JSON.parse(new TextDecoder().decode(defaultFiles['build.json'])).settings, {graphics:'colorful'});
  const originalKit = (await fixture()).kit;
  assert.equal(new TextDecoder().decode(defaultFiles['content/display/probe.svg']),
    originalKit.screenTemplate.replace('</svg>', originalKit.machines[0].artwork + '</svg>'));
  graphicsInput.checked = true; graphicsInput.onchange();
  await $('preview-selected').onclick();
  assert.equal($('selected-previews').open, true);
  assert.equal($('selected-previews-list').children.length, 1); // Excludes unselected 'other'.
  const previewImage = () => $('selected-previews-list').children[0].children[1].children[0];
  const grayPreviewUrl = previewImage().src;
  const grayPreview = await (await fetch(grayPreviewUrl)).text();
  assert.equal(grayPreview, (await fixture()).kit.screenTemplate.replace('</svg>', Core.selectArtwork((await fixture()).kit.machines[0], 'grayscale') + '</svg>'));
  assert.equal($('selected-previews-count').textContent, '1 machine selected');
  const bulkControl = () => $('selected-previews-list').children[0].children[3].children[1];
  const beforeAll = invalidations;
  $('graphics-all-original').onclick();
  assert.equal($('selected-previews').open, true);
  assert.equal(graphicsInput.checked, false);
  assert.equal(bulkControl().value, 'colorful');
  assert.deepEqual(bulkControl().children.map(option => option.textContent), ['Enforce grayscale', 'Keep original design']);
  assert.ok((await (await fetch(previewImage().src)).text()).includes(artwork));
  assert.equal(invalidations, beforeAll + 1);
  await assert.rejects(fetch(grayPreviewUrl));
  $('graphics-all-grayscale').onclick();
  assert.equal(graphicsInput.checked, true);
  assert.equal(await (await fetch(previewImage().src)).text(), grayPreview);
  $('graphics-all-original').onclick();
  $('close-selected-previews').onclick();
  assert.equal($('selected-previews-list').children.length, 0);
  await assert.rejects(fetch(grayPreviewUrl));
  await $('build').onclick();
  const allOriginalFiles = wizardOptions.getFiles();
  assert.deepEqual(JSON.parse(new TextDecoder().decode(allOriginalFiles['build.json'])).settings, {graphics:'colorful'});
  assert.equal(new TextDecoder().decode(allOriginalFiles['content/display/probe.svg']),
    originalKit.screenTemplate.replace('</svg>', originalKit.machines[0].artwork + '</svg>'));
  await $('preview-selected').onclick();
  assert.ok((await (await fetch(previewImage().src)).text()).includes(artwork));
  $('graphics-all-grayscale').onclick();
  $('close-selected-previews').onclick();
  const mixed = await fixture();
  mixed.kit.machines.push({id:'stock', name:'Stock-screen machine'});
  await api.configure({...mixed, selected:['probe','other','stock']});
  await $('preview-selected').onclick();
  assert.equal($('graphics-all-grayscale').disabled, false);
  const figures = $('selected-previews-list').children;
  assert.equal(figures.length, 3);
  assert.match(figures[1].children[1].children[0].textContent, /Preview unavailable.*Unsupported SVG paint/);
  assert.match(figures[2].children[1].children[0].textContent, /uses a stock screen/);
  assert.ok(figures[0].children[1].children[0].src); // Other failures leave valid previews intact.
  $('selected-previews').close();
  await api.configure({...mixed, selected:[]});
  assert.equal($('preview-selected').disabled, true);
  await $('preview-selected').onclick();
  assert.equal($('selected-previews').open, false);
  await api.configure(await fixture());
  $('choose-extensions').onclick();
  assert.equal(pages[2].hidden, false);
  assert.equal(pages[3].hidden, true);
  const files = () => wizardOptions.getFiles();
  const manifest = () => JSON.parse(new TextDecoder().decode(files()['build.json']));
  const setGraphics = value => {
    graphicsInput.checked = value === 'grayscale';
    graphicsInput.onchange();
  };
  await $('build').onclick();
  assert.deepEqual(manifest().settings, {graphics:'grayscale'});
  assert.deepEqual(JSON.parse(new TextDecoder().decode(files()['layout.json'])).extensions, []);
  assert.ok(paintValues(new TextDecoder().decode(files()['content/display/probe.svg'])).every(isGray));
  const beforeOverride = $('download').href;
  const override = () => $('selected-previews-list').children[0].children[3].children[1];
  await $('preview-selected').onclick();
  override().value = 'colorful'; override().onchange();
  $('selected-previews').close();
  assert.equal(files(), null);
  await assert.rejects(fetch(beforeOverride));
  assert.equal(graphicsInput.checked, false); // The only custom screen now uses its original design.
  await $('build').onclick();
  assert.deepEqual(manifest().settings, {graphics:'grayscale', graphicsOverrides:{probe:'colorful'}});
  assert.doesNotMatch($('download-summary').textContent, /Grayscale machine graphics/);
  assert.ok(paintValues(new TextDecoder().decode(files()['content/display/probe.svg'])).some(value => !isGray(value)));
  await $('preview-selected').onclick();
  override().value = 'grayscale'; override().onchange();
  assert.equal(graphicsInput.checked, true);
  $('selected-previews').close();
  await $('build').onclick();
  assert.deepEqual(manifest().settings, {graphics:'grayscale'});
  setGraphics('colorful');
  assert.equal(previewModes.at(-1), 'colorful');
  await $('preview-selected').onclick();
  const originalPreviewUrl = previewImage().src;
  assert.ok((await (await fetch(originalPreviewUrl)).text()).includes(artwork));
  assert.equal($('selected-previews-count').textContent, '1 machine selected');
  $('selected-previews').close(); // Native Escape also emits close and releases assets.
  await assert.rejects(fetch(originalPreviewUrl));
  await $('build').onclick();
  assert.equal(pages[3].hidden, false);
  assert.deepEqual(manifest().settings, {graphics:'colorful'});
  const original = files()['content/display/probe.svg'].slice();
  assert.ok(wizardOptions.hasDownload());
  const firstUrl = $('download').href;
  setGraphics('grayscale');
  assert.equal(previewModes.at(-1), 'grayscale');
  assert.ok(invalidations > 0);
  assert.equal(wizardOptions.hasDownload(), false);
  assert.equal(files(), null);
  assert.equal($('download').href, undefined);
  assert.equal($('flow-download').href, undefined);
  assert.equal(steps[3].disabled, true);
  await assert.rejects(fetch(firstUrl)); // The previous download URL was revoked.
  await $('build').onclick();
  assert.deepEqual(manifest().settings, {graphics:'grayscale'});
  const screen = files()['content/display/probe.svg'];
  assert.ok(paintValues(new TextDecoder().decode(screen)).every(isGray));
  assert.equal(manifest().files['content/display/probe.svg'], await sha256(screen));
  assert.deepEqual(new Uint8Array(await (await fetch($('download').href)).arrayBuffer()), Core.zipFiles(files()));
  assert.equal($('download').href, $('flow-download').href);
  setGraphics('colorful');
  await $('build').onclick();
  assert.deepEqual(files()['content/display/probe.svg'], original);
  // A conversion setting changed while packaging must never publish stale files.
  setGraphics('grayscale'); blockPatch = true;
  const pending = $('build').onclick();
  while (!finishPatch) await new Promise(resolve => setTimeout(resolve, 0));
  setGraphics('colorful'); finishPatch(Uint8Array.of(1, 2, 3)); await pending;
  assert.equal(files(), null);
  assert.equal(wizardOptions.hasDownload(), false);
  assert.equal(steps[3].disabled, true);
  finishPatch = undefined;
  await $('preview-selected').onclick();
  override().value = 'grayscale'; override().onchange();
  $('selected-previews').close();
  blockPatch = false;
  await $('build').onclick();
  assert.deepEqual(manifest().settings, {graphics:'colorful', graphicsOverrides:{probe:'grayscale'}});
  assert.match($('download-summary').textContent, /Grayscale machine graphics/);
  blockPatch = true;
  const pendingOverride = $('build').onclick();
  while (!finishPatch) await new Promise(resolve => setTimeout(resolve, 0));
  await $('preview-selected').onclick();
  override().value = 'colorful'; override().onchange();
  $('selected-previews').close();
  finishPatch(Uint8Array.of(1, 2, 3)); await pendingOverride;
  assert.equal(files(), null);
  blockPatch = false;
  await $('build').onclick();
  assert.deepEqual(manifest().settings, {graphics:'colorful'});
  assert.doesNotMatch($('download-summary').textContent, /Grayscale machine graphics/);
});
