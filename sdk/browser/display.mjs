// Generic main-thread host for the bounded retained-asset drawing contract.
// Animation/state math lives entirely in the compiled contributor callback.
import { inputWords } from './controls.mjs';

// draw::Input: parameters[4], elapsed, track, note_serial, reduced_motion,
// shown[4 bytes], slots[8], held_notes, note_off_serial, shift, choices[4 bytes].
// ABI 1 ends after slots, ABI 2 after note_off_serial.
const INPUT_BYTES = {1: 68, 2: 76, 3: 84};
const integer = (value, low, high, name) => {
  if (!Number.isInteger(value) || value < low || value > high) throw new Error(`Invalid ${name}`);
  return value;
};

// Main-thread note gates, independent of audition voices and their release tails.
// One instance per track. Multiple input sources for one pitch are combined by
// the caller before publishing noteOn/noteOff, just like stock's per-pitch map.
export class NoteActivity {
  constructor() {
    this.held = new Set();
    this.note_on_serial = 0;
    this.note_off_serial = 0;
  }
  get held_notes() { return this.held.size; }
  noteOn(note) {
    if (!Number.isInteger(note) || note < 0 || note > 127) return false;
    this.held.add(note);
    this.note_on_serial = this.note_on_serial + 1 >>> 0;
    return true;
  }
  noteOff(note) {
    if (!this.held.delete(note)) return false;
    this.note_off_serial = this.note_off_serial + 1 >>> 0;
    return true;
  }
  panic() {
    this.note_off_serial = this.note_off_serial + this.held.size >>> 0;
    this.held.clear();
  }
}

// Outlines the OP-XY draws as streaks: a line traced out and back over itself,
// which has no area, or an outline that turns straight back on itself at a point.
// `frame` is a PreviewDisplay frame with `paths` outlines of `points` points.
export function outlineFaults(frame, paths, points) {
  const faults = [];
  for (let p = 0; p < paths; ++p) {
    const at = p * (1 + 2 * points) + 1, xy = [];
    for (let i = 0; i < points; ++i) {
      const x = frame[at + 2 * i], y = frame[at + 2 * i + 1], last = xy[xy.length - 1];
      if (!last || last[0] !== x || last[1] !== y) xy.push([x, y]);
    }
    const first = xy[0], end = xy[xy.length - 1];
    if (xy.length > 1 && first[0] === end[0] && first[1] === end[1]) xy.pop();
    if (xy.length < 3) continue;  // a point or a collapsed outline draws nothing
    // Traced out and back: every edge is also walked the other way. (A signed area
    // of zero would also flag a figure eight, which encloses area.)
    const edges = new Map(), edge = (a, b) => `${a[0]},${a[1]}>${b[0]},${b[1]}`;
    for (let i = 0; i < xy.length; ++i) {
      const key = edge(xy[i], xy[(i + 1) % xy.length]);
      edges.set(key, (edges.get(key) || 0) + 1);
    }
    const retraced = [...edges].every(([key, count]) => {
      const [a, b] = key.split('>');
      return edges.get(`${b}>${a}`) === count;
    });
    if (retraced) { faults.push(`outline ${p} has no area`); continue; }
    for (let i = 0; i < xy.length; ++i) {
      const before = xy[(i + xy.length - 1) % xy.length], after = xy[(i + 1) % xy.length];
      if (before[0] === after[0] && before[1] === after[1]) { faults.push(`outline ${p} turns straight back`); break; }
    }
  }
  return faults;
}

export class PreviewDisplay {
  constructor(module) {
    if (!(module instanceof WebAssembly.Module) || WebAssembly.Module.imports(module).length) {
      throw new Error('Display requires a self-contained compiled module');
    }
    this.api = new WebAssembly.Instance(module).exports;
    const e = this.api;
    const abi = e.opxy_display_abi(), inputBytes = e.opxy_display_input_bytes();
    this.encoderActivity = abi >= 6 && abi <= 8;
    const baseAbi = this.encoderActivity ? abi - 3 : abi;
    const baseBytes = inputBytes - (this.encoderActivity ? 8 : 0);
    this.encoderAt = this.encoderActivity ? baseBytes / 4 : 0;
    const sizes = {...INPUT_BYTES, 4: 152, 5: 1132};
    if (sizes[baseAbi] !== baseBytes) throw new Error('Unsupported drawing ABI');
    this.noteActivity = baseAbi >= 2;
    this.effectRow = baseAbi >= 3;
    this.audioCapacity = baseAbi === 5 ? 256 : 16;
    this.extendedAudio = baseAbi === 5;
    this.audioWords = baseAbi >= 4 ? integer(e.opxy_display_audio_words(), 1, this.audioCapacity, 'display telemetry words') : 0;
    this.stateBytes = integer(e.opxy_display_state_bytes(), 4, e.memory.buffer.byteLength, 'display state size');
    const frameBytes = integer(e.opxy_display_frame_bytes(), 32, 32 * 8 + 128 * 20 + 256 * 32 + 32 * 36 + 32 + 64 * 4, 'drawing frame size');
    this.memory = e.memory.buffer;
    const ranges = [
      [e.opxy_display_state(), this.stateBytes], [e.opxy_display_input(), inputBytes], [e.opxy_display_frame(), frameBytes],
    ];
    for (let i = 0; i < ranges.length; i++) {
      const [at, size] = ranges[i];
      integer(at, 4, this.memory.byteLength - size, 'display storage');
      if (at % 4 || size % 4) throw new Error('Misaligned drawing storage');
      for (let j = 0; j < i; j++) {
        if (at < ranges[j][0] + ranges[j][1] && ranges[j][0] < at + size) throw new Error('Overlapping drawing storage');
      }
    }
    this.input = new Uint32Array(this.memory, ranges[1][0], inputBytes / 4);
    this.frame = new Int32Array(this.memory, ranges[2][0], frameBytes / 4);
    this.api.opxy_control_source?.(1);
    this.reset();
  }
  reset() { this.api.opxy_display_reset(); }
  controlInteraction(track, physical, parameter, value, event) {
    integer(track, 0, 7, 'interaction track');
    integer(physical, 0, 3, 'physical knob');
    integer(parameter, 0, 7, 'interaction parameter');
    integer(value, 0, 0x7fffffff, 'interaction normalized value');
    if (!event?.isTrusted || !(event.type === 'input' || event.type === 'pointerdown' || event.type === 'pointermove' ||
        event.type === 'keydown' && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End','PageUp','PageDown'].includes(event.key))) return false;
    if (typeof this.api.opxy_control_source !== 'function' || typeof this.api.opxy_control_event !== 'function') return false;
    this.api.opxy_control_source(1);
    return this.api.opxy_control_event(track, physical, parameter, value) === 1;
  }
  audioResetRequested() { return this.extendedAudio && this.input[282] !== 0; }
  audioResetTarget() { return this.extendedAudio ? this.input[282] >>> 0 : 0; }
  // input: shownControls(words, shown) from controls.mjs.
  tick(input, elapsed, serial, track = 0, reducedMotion = false, activity = null, shiftRow = null, audio = null, encoders = null) {
    if (typeof reducedMotion !== 'boolean') throw new Error('Invalid display input');
    // Validate the complete snapshot before mutating input/state.
    const [p0, p1, p2, p3, shown, ...slots] = inputWords(input);
    integer(elapsed, -0x80000000, 0x7fffffff, 'elapsed Q10 seconds');
    integer(serial, 0, 0xffffffff, 'note serial');
    integer(track, 0, 0xffffffff, 'track identity');
    const held = activity === null ? 0xffffffff : integer(activity.held_notes, 0, 128, 'held note count');
    const released = activity === null ? 0 : integer(activity.note_off_serial, 0, 0xffffffff, 'note-off serial');
    const shift = shiftRow === null ? 0 : Number(Boolean(shiftRow.shift));
    const choices = shiftRow === null ? [0, 0, 0, 0] : shiftRow.choices.map(c => integer(c, 0, 255, 'shift choice'));
    if (choices.length !== 4) throw new Error('Invalid shift row');
    const available = encoders === null ? 0 : integer(encoders.available, 0, 1, 'encoder availability');
    const mask = encoders === null ? 0 : integer(encoders.mask, 0, 15, 'encoder mask');
    if (!available && mask) throw new Error('Unavailable encoder snapshot must be empty');
    let words = Array(this.audioCapacity).fill(0), count = 0;
    let sequence=0, requested=0, applied=0, resetSupported=0;
    if (audio !== null) {
      count = integer(audio.count, 0, this.audioWords, 'audio telemetry count');
      if (count !== 0 && count !== this.audioWords || !Array.isArray(audio.words) || audio.words.length !== this.audioCapacity)
        throw new Error('Invalid audio telemetry snapshot');
      words = audio.words.map(word => integer(word, 0, 0xffffffff, 'audio telemetry word'));
      if (this.extendedAudio) {
        [sequence,requested,applied] = ['sequence','requested','applied'].map(key => integer(audio[key] ?? 0, 0, 0xffffffff, 'audio '+key));
        resetSupported = integer(audio.resetSupported ?? 0, 0, 1, 'audio reset capability');
      }
    }
    if (this.api.memory.buffer !== this.memory) throw new Error('Display memory changed');
    this.input.set([p0, p1, p2, p3, elapsed >>> 0, track, serial, Number(reducedMotion), shown, ...slots]);
    if (this.noteActivity) this.input.set([held, released], 17);
    if (this.effectRow) this.input.set([shift, choices[0] | choices[1] << 8 | choices[2] << 16 | (choices[3] << 24) >>> 0], 19);
    if (this.audioWords) this.input.set([...words, count], 21);
    if (this.extendedAudio) this.input.set([sequence,requested,applied,resetSupported,0], 278);
    if (this.encoderActivity) this.input.set([available, mask], this.encoderAt);
    this.api.opxy_display_render();
    if (this.api.opxy_display_valid() !== 1) throw new Error('Display emitted an invalid drawing frame');
    return this.frame;
  }
}

const SVG = 'http://www.w3.org/2000/svg';
const decimal = word => String(Math.round(word / 1024 * 1000) / 1000);
// floor(value / 1024), as the C++ floor_q10, for products inside int32.
const floorQ10 = value => Math.floor(value / 1024);
const decoder = new TextDecoder();
// Elements and attributes of the authored subset the preview copies; never
// styles, links, event handlers or unknown XML.
const elements = new Set(['g', 'rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'text']);
const attributes = new Set(['id', 'd', 'x', 'y', 'width', 'height', 'cx', 'cy', 'r', 'rx', 'ry', 'x1', 'y1', 'x2', 'y2',
  'points', 'fill', 'stroke', 'fill-opacity', 'stroke-opacity', 'stroke-width', 'stroke-linejoin', 'stroke-linecap', 'stroke-miterlimit', 'transform',
  'font-family', 'font-size', 'text-anchor']);

const escape = text => String(text).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
// The device page's four-control header, as the SDK screen template draws it
// (sdk/platform/opxy-1.2.1/screen.svg): a stepped grey bar 20 px tall with each
// position's title above its value, light text on the dark half and dark text
// on the light half. Browser fonts stand in for the device's.
export function headerSvg(titles, values) {
  const steps = ['#000000', '#323232', '#4B4B4B', '#646464', '#7D7D7D', '#969696', '#AFAFAF', '#CDCDCD'];
  const bar = steps.map((fill, i) => `<rect x="${i * 60}" width="60" height="20" fill="${fill}"/>`).join('');
  const text = [0, 1, 2, 3].map(i => {
    const ink = i < 2 ? '#CBCBCB' : '#000000', x = 120 * i;
    return `<text x="${x + 5}" y="10" fill="${ink}" font-size="10">${escape(titles[i] ?? '')}</text>` +
      `<text x="${x + 65}" y="17.44" fill="${ink}" font-size="20">${escape(values[i] ?? '')}</text>`;
  }).join('');
  return `<g data-opxy-header="" font-family="sans-serif">${bar}${text}</g>`;
}

// The whole artwork in authored order, with the frame's nodes, text slots and
// palette entries bound by ID, as on the device.
// Strict bounded source grammar. Cache normalized local Q10 geometry once;
// selections never parse SVG and never borrow a mutable instance's outline.
function glyphCommands(data) {
  const token = /[MLCZ]|[-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)/g;
  if (data.replace(token, '').replace(/[\s,]/g, '')) throw new Error('Unsupported glyph grammar');
  const tokens = data.match(token) || [], records = [];
  let i = 0, closed = true;
  while (i < tokens.length) {
    const verb = tokens[i++], count = {M:2,L:2,C:6,Z:0}[verb];
    if (count === undefined || closed && verb !== 'M' || i + count > tokens.length) throw new Error('Invalid glyph contour');
    const values = tokens.slice(i, i + count).map(Number); i += count;
    if (values.some(v => !Number.isFinite(v) || Math.abs(v) > 8192)) throw new Error('Invalid glyph coordinate');
    records.push([verb, values]); closed = verb === 'Z';
    if (records.length > 16) throw new Error('Glyph command capacity exceeded');
  }
  if (!records.length) throw new Error('Empty glyph source');
  return records;
}
export class SvgDisplay {
  constructor(asset, bindings) {
    if (typeof asset !== 'string' || asset.length > 65536 || /<!DOCTYPE|<!ENTITY/i.test(asset)) {
      throw new Error('Invalid drawing asset');
    }
    const { nodes, texts, palette, paths, points, affine = 0, fullCanvas = 0, rgba = 0, cubic = 0, openPaths = 0, segmentPaths = 0, quadratic = 0, commands = 0, countedPaths = 0, glyphs = 0, glyphDefinitions = 0, glyphStart = 0, glyphSources = [] } = bindings;
    integer(affine, 0, 1, 'affine display'); integer(fullCanvas, 0, 1, 'full canvas');
    integer(cubic, 0, 1, 'cubic display'); integer(openPaths, 0, 0xffffffff, 'open path mask');
    integer(countedPaths, 0, 1, 'counted paths'); this.counted = countedPaths === 1;
    integer(quadratic, 0, 1, 'quadratic display'); integer(commands, 0, 128, 'quadratic command capacity');
    integer(segmentPaths, 0, 0xffffffff, 'segment path mask');
    this.affine = affine === 1; this.fullCanvas = fullCanvas === 1;
    integer(rgba, 0, 1, 'RGBA display'); this.rgba = rgba === 1;
    if (this.rgba && (!this.affine || palette.length)) throw new Error('RGBA requires affine nodes and palette zero');
    this.cubic = cubic === 1; this.openPaths = openPaths;
    this.quadratic = quadratic === 1; this.commands = commands;
    this.segmentPaths = segmentPaths;
    if (!Array.isArray(nodes) || !Array.isArray(texts) || !Array.isArray(palette) ||
        !Number.isInteger(paths) || paths < 0 || paths > 32 || paths > nodes.length ||
        !Number.isInteger(points) || (quadratic ? points !== 0 || !paths || !affine || commands < 2 || cubic || openPaths || segmentPaths :
          commands !== 0 || (paths ? points < 2 || points > 256 : points !== 0)) ||
        (countedPaths ? !affine || !paths || quadratic || cubic || paths * (8 + 8 * points) > 2816 :
          paths * (quadratic ? 2 * commands : points) > 256) || openPaths >= 2 ** paths || segmentPaths >= 2 ** paths || (openPaths & segmentPaths) ||
        (cubic && (!paths || points !== 4 || openPaths || segmentPaths)) ||
        nodes.length < 1 || nodes.length > 256 || texts.length > 32 || palette.length < (affine ? 0 : 1) || palette.length > 16) {
      throw new Error('Unsupported retained asset bindings');
    }
    integer(glyphs, 0, 64, 'glyph instance count');
    integer(glyphDefinitions, 0, 16, 'glyph definition count');
    integer(glyphStart, 0, 255, 'glyph start');
    if (!Array.isArray(glyphSources) || glyphSources.length !== 2 * glyphDefinitions ||
        (glyphs ? !affine || !glyphDefinitions || glyphStart < paths || glyphStart + 2 * glyphs > nodes.length : glyphDefinitions || glyphStart))
      throw new Error('Invalid owned glyph atlas budget');
    this.glyphStart = glyphStart; this.glyphCount = glyphs; this.glyphDefinitions = glyphDefinitions;
    this.points = points;
    // Frame words: paths (key, points), nodes (7 words), texts (3 words and 24
    // bytes), then the header's four 8-byte strings. An empty path or text
    // array still holds one unused element, as in draw.hpp.
    this.nodesAt = Math.max(paths, 1) * (quadratic ? 2 + 5 * commands : (countedPaths ? 2 : 1) + 2 * points);
    this.nodeWords = affine ? 8 : 7;
    this.textsAt = this.nodesAt + this.nodeWords * nodes.length;
    this.headerAt = this.textsAt + 9 * Math.max(texts.length, 1);
    this.glyphsAt = this.headerAt + 8;
    this.frameWords = this.glyphsAt + glyphs;
    const ids = [...nodes, ...texts, ...palette, ...glyphSources];
    if (new Set(ids).size !== ids.length || ids.some(id => !/^[a-zA-Z_][a-zA-Z0-9_]*$/.test(id))) {
      throw new Error('Invalid retained asset identities');
    }
    const parsed = new DOMParser().parseFromString(`<svg xmlns="${SVG}">${asset}</svg>`, 'image/svg+xml');
    if (parsed.querySelector('parsererror')) throw new Error('Malformed drawing asset');
    this.root = document.createElementNS(SVG, 'g');
    this.root.setAttribute('data-opxy-display', 'cpp');
    const copies = new Map();
    const copy = (source, parent) => {
      for (const child of source.children) {
        if (!elements.has(child.localName)) continue;
        const node = document.createElementNS(SVG, child.localName);
        for (const attribute of child.attributes) {
          if (attributes.has(attribute.name)) node.setAttribute(attribute.name, attribute.value);
        }
        if (child.localName === 'text') node.textContent = child.textContent;
        else copy(child, node);
        if (child.id) {
          if (copies.has(child.id)) throw new Error('Duplicate drawing asset ID');
          copies.set(child.id, node);
        }
        parent.append(node);
      }
    };
    copy(parsed.documentElement, this.root);
    const leaf = (id, types) => {
      const element = copies.get(id);
      if (!element || !types.includes(element.localName)) throw new Error(`Missing drawing leaf ${id}`);
      return element;
    };
    // A palette entry lends its fill, stroke and stroke width, as on the device.
    this.palette = palette.map(id => {
      const element = leaf(id, ['path', 'circle']);
      const fill = element.getAttribute('fill'), stroke = element.getAttribute('stroke');
      if (![fill, stroke].every(value => /^#[0-9a-fA-F]{6}$/.test(value))) throw new Error('Unsupported drawing paint');
      const width = Number(element.getAttribute('stroke-width'));
      if (!(width > 0 && width <= 16)) throw new Error('Unsupported stroke width');
      return { fill, stroke, width };
    });
    this.nodes = nodes.map(id => leaf(id, ['path', 'circle']));
    this.authoredVisibility = this.nodes.map(node => node.getAttribute('visibility'));
    this.glyphOutlines = glyphSources.map((id, i) => {
      const source = leaf(id, ['path']);
      if (source.getAttribute('transform')) throw new Error('Transformed glyph source');
      const records = glyphCommands(source.getAttribute('d') || '');
      if (!(i % 2) && records.length < 2) throw new Error('Empty first glyph part');
      return records.length === 1 ? '' : records.map(([verb, values]) => verb + values.map(v => String(Math.trunc(v * 1024) / 1024)).join(' ')).join(' ');
    });
    for (let i = glyphStart; i < glyphStart + 2 * glyphs; ++i) {
      const node = this.nodes[i];
      if (node.localName !== 'path' || node.getAttribute('fill') !== 'none') throw new Error('Glyph instance must own a stroke-only path');
      const records = glyphCommands(node.getAttribute('d') || '');
      if (records.length !== 16 || records.some(([verb], k) => verb !== (k ? 'L' : 'M')) ||
          new Set(records.map(([, values]) => values.join(','))).size !== 16) throw new Error('Invalid glyph scaffold');
    }
    this.glyphApplied = Array(glyphs).fill(-1);
    if (this.rgba) for (const node of this.nodes) {
      if (node.getAttribute('opacity') !== null) throw new Error('RGBA requires separate fill/stroke opacity');
      for (const paint of ['fill', 'stroke']) {
        const color = node.getAttribute(paint) || 'none';
        const alpha = Number(node.getAttribute(`${paint}-opacity`) ?? 1);
        if (color !== 'none' && (!/^#[0-9a-fA-F]{6}$/.test(color) || !(alpha > 0 && alpha <= 1))) {
          throw new Error('RGBA requires an explicit allocated solid paint');
        }
      }
    }

    // The device renderer only changes a fill the artwork already has.
    this.fillable = this.nodes.map(node => (node.getAttribute('fill') || 'none') !== 'none');
    this.strokeable = this.nodes.map(node => (node.getAttribute('stroke') || 'none') !== 'none');
    this.paths = this.nodes.slice(0, paths);
    if (this.paths.some(node => node.localName !== 'path')) throw new Error('Mutable asset must be a path');
    this.texts = texts.map(id => leaf(id, ['text']));
    this.keys = this.paths.map(() => 0);
    // What each node and text slot last applied, so unchanged ones cost nothing.
    this.applied = this.nodes.map(() => '');
    this.shown = this.texts.map(() => '');
    // The header's text for each control position; '' keeps the stock value.
    this.header = ['', '', '', ''];
  }
  text(frame, word, bytes) {
    const view = new Uint8Array(frame.buffer, frame.byteOffset + word * 4, bytes);
    const end = view.indexOf(0);
    if (end < 0) throw new Error('Unterminated display text');
    return decoder.decode(view.subarray(0, end));
  }
  apply(frame) {
    if (!(frame instanceof Int32Array) || frame.length !== this.frameWords) throw new Error('Invalid drawing frame buffer');
    const stride = this.quadratic ? 2 + 5 * this.commands : (this.counted ? 2 : 1) + 2 * this.points;
    if (this.counted) {
      let active = 0;
      for (let i = 0; i < this.paths.length; ++i)
        active += integer(frame[i * stride + 1], 2, this.points, 'active path points');
      if (active > 256) throw new Error('Active path point budget exceeds 256');
    }
    // Validate every selector before any retained node is changed.
    for (let i = 0; i < this.glyphCount; ++i) integer(frame[this.glyphsAt + i], 0, this.glyphDefinitions - 1, 'glyph selector');
    for (let i = 0; i < this.glyphCount; ++i) {
      const selected = frame[this.glyphsAt + i];
      if (this.glyphApplied[i] === selected) continue;
      for (let p = 0; p < 2; ++p) this.nodes[this.glyphStart + 2*i + p].setAttribute('d', this.glyphOutlines[2*selected+p]);
      this.glyphApplied[i] = selected;
    }
    // Callers use PreviewDisplay, which runs the same native frame validator.
    for (let path = 0; path < this.paths.length; path++) {
      const offset = path * stride, key = frame[offset];
      if (this.keys[path] === key) continue;
      const coordinate = (this.affine || this.cubic) ? word => String(word / 1024) : decimal;
      const points = this.counted ? frame[offset + 1] : this.points;
      const first = offset + (this.counted ? 2 : 1);
      const pair = i => `${coordinate(frame[first + i * 2])} ${coordinate(frame[first + 1 + i * 2])}`;
      let outline;
      if (this.quadratic) {
        const count = integer(frame[offset + 1], 2, this.commands, 'quadratic command count');
        const pieces = [];
        for (let i = 0; i < count; ++i) {
          const at = offset + 2 + 5 * i, kind = frame[at];
          if (i === 0 ? kind !== 1 : kind !== 2 && kind !== 3) throw new Error('Invalid quadratic path command');
          const end = `${coordinate(frame[at + 3])} ${coordinate(frame[at + 4])}`;
          pieces.push(kind === 3 ? `Q${coordinate(frame[at + 1])} ${coordinate(frame[at + 2])} ${end}` : `${kind === 1 ? 'M' : 'L'}${end}`);
        }
        outline = pieces.join(' ');
      } else if (this.cubic) outline = `M${pair(0)} C${pair(1)} ${pair(2)} ${pair(3)}`;
      else if ((this.segmentPaths >>> path) & 1)
        outline = Array.from({length:points - 1}, (_, i) => `M${pair(i)} L${pair(i + 1)}`).join(' ');
      else outline = Array.from({length:points}, (_, i) => `${i ? 'L' : 'M'}${pair(i)}`).join(' ') +
          ((this.openPaths >>> path) & 1 ? '' : ' Z');
      this.paths[path].setAttribute('d', outline);
      this.keys[path] = key;
    }
    for (let i = 0; i < this.nodes.length; i++) {
      const offset = this.nodesAt + i * this.nodeWords;
      const words = frame.subarray(offset, offset + this.nodeWords), key = words.join();
      if (this.applied[i] === key) continue;
      this.applied[i] = key;
      let a, b, c, d, x, y, shade, paint;
      if (this.affine) [a, b, c, d, x, y, shade, paint] = words;
      else {
        const [tx, ty, scale, cos, sin, color, mode] = words;
        a = floorQ10(scale * cos); b = floorQ10(scale * sin); c = -b; d = a;
        x = tx; y = ty; shade = color; paint = mode;
      }
      const node = this.nodes[i];
      if (paint === 6) {
        this.concealed ??= [];
        this.concealed[i] = true;
        node.setAttribute('visibility', 'hidden'); continue;
      }
      if (this.concealed?.[i]) {
        if (this.authoredVisibility[i] === null) node.removeAttribute('visibility');
        else node.setAttribute('visibility', this.authoredVisibility[i]);
        this.concealed[i] = false;
      }
      // XY transforms geometry before stroking at the original logical width.
      if (this.affine) node.setAttribute('vector-effect', 'non-scaling-stroke');
      const coordinate = this.affine ? word => String(word / 1024) : decimal;
      node.setAttribute('transform',
        `matrix(${coordinate(a)} ${coordinate(b)} ${coordinate(c)} ${coordinate(d)} ${coordinate(x)} ${coordinate(y)})`);
      if (this.rgba && paint >= 3) {
        const packed = shade >>> 0;
        const color = '#' + [packed & 255, (packed >>> 8) & 255, (packed >>> 16) & 255]
          .map(value => value.toString(16).padStart(2, '0')).join('');
        const alpha = String((packed >>> 24) / 255);
        if (paint !== 5 && this.strokeable[i]) {
          node.setAttribute('stroke', color); node.setAttribute('stroke-opacity', alpha);
        }
        if (paint !== 3 && this.fillable[i]) {
          node.setAttribute('fill', color); node.setAttribute('fill-opacity', alpha);
        }
      } else if (paint) {
        const entry = this.palette[shade];
        node.setAttribute('stroke', entry.stroke);
        node.setAttribute('stroke-width', String(entry.width));
        if (paint === 2 && this.fillable[i]) node.setAttribute('fill', entry.fill);
      }
    }
    for (let i = 0; i < this.texts.length; i++) {
      const offset = this.textsAt + i * 9, [x, y, scale] = frame.subarray(offset, offset + 3);
      const content = this.text(frame, offset + 3, 24), key = `${x},${y},${scale},${content}`;
      if (this.shown[i] === key) continue;
      this.shown[i] = key;
      const node = this.texts[i];
      node.setAttribute('transform', `matrix(${decimal(scale)} 0 0 ${decimal(scale)} ${decimal(x)} ${decimal(y)})`);
      node.textContent = content;
    }
    this.header = [0, 1, 2, 3].map(i => this.text(frame, this.headerAt + 2 * i, 8));
  }
  svg() {
    const artwork = new XMLSerializer().serializeToString(this.root);
    if (!this.affine) return artwork;
    // An image establishes a fixed 480x220 logical viewport. Its non-scaling
    // strokes ignore node matrices, while the containing responsive screen
    // scales the entire image (including stroke widths) as one panel.
    const svg = `<svg xmlns="${SVG}" width="480" height="220" viewBox="0 0 480 220">${artwork}</svg>`;
    return `<image data-opxy-display="cpp" width="480" height="220" href="data:image/svg+xml,${encodeURIComponent(svg)}"/>`;
  }
}
