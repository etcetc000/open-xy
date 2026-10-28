// Generic main-thread host for the bounded retained-asset drawing contract.
// Animation/state math lives entirely in the compiled contributor callback.
import { inputWords } from './controls.mjs';

// draw::Input: parameters[4], elapsed, track, note_serial, reduced_motion,
// shown[4 bytes], slots[8], held_notes, note_off_serial. ABI 1 ends after slots.
const INPUT_BYTES = 76;
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
    if (!(abi === 1 && inputBytes === 68 || abi === 2 && inputBytes === INPUT_BYTES)) throw new Error('Unsupported drawing ABI');
    this.noteActivity = abi === 2;
    this.stateBytes = integer(e.opxy_display_state_bytes(), 4, e.memory.buffer.byteLength, 'display state size');
    // Largest declared frame: 4 paths of 64 points, 256 nodes, 16 texts and the header.
    const frameBytes = integer(e.opxy_display_frame_bytes(), 32, 4 * 516 + 256 * 28 + 16 * 36 + 32, 'drawing frame size');
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
    this.reset();
  }
  reset() { this.api.opxy_display_reset(); }
  // GUI activity for a silent sample-page preview; no PCM or audio emulation.
  setActivity(voices) { this.api.opxy_display_activity?.(integer(voices, 0, 256, 'preview voices')); }
  // input: shownControls(words, shown) from controls.mjs.
  tick(input, elapsed, serial, track = 0, reducedMotion = false, activity = null) {
    if (typeof reducedMotion !== 'boolean') throw new Error('Invalid display input');
    // Validate the complete snapshot before mutating input/state.
    const [p0, p1, p2, p3, shown, ...slots] = inputWords(input);
    integer(elapsed, -0x80000000, 0x7fffffff, 'elapsed Q10 seconds');
    integer(serial, 0, 0xffffffff, 'note serial');
    integer(track, 0, 0xffffffff, 'track identity');
    const held = activity === null ? 0xffffffff : integer(activity.held_notes, 0, 128, 'held note count');
    const released = activity === null ? 0 : integer(activity.note_off_serial, 0, 0xffffffff, 'note-off serial');
    if (this.api.memory.buffer !== this.memory) throw new Error('Display memory changed');
    this.input.set([p0, p1, p2, p3, elapsed >>> 0, track, serial, Number(reducedMotion), shown, ...slots]);
    if (this.noteActivity) this.input.set([held, released], 17);
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
  'points', 'fill', 'stroke', 'stroke-width', 'stroke-linejoin', 'stroke-linecap', 'stroke-miterlimit', 'transform',
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
export class SvgDisplay {
  constructor(asset, bindings) {
    if (typeof asset !== 'string' || asset.length > 65536 || /<!DOCTYPE|<!ENTITY/i.test(asset)) {
      throw new Error('Invalid drawing asset');
    }
    const { nodes, texts, palette, paths, points } = bindings;
    if (!Array.isArray(nodes) || !Array.isArray(texts) || !Array.isArray(palette) ||
        !Number.isInteger(paths) || paths < 0 || paths > 4 || paths > nodes.length ||
        !Number.isInteger(points) || (paths ? points < 2 || points > 64 : points !== 0) ||
        nodes.length < 1 || nodes.length > 256 || texts.length > 16 || palette.length < 1 || palette.length > 16) {
      throw new Error('Unsupported retained asset bindings');
    }
    this.points = points;
    // Frame words: paths (key, points), nodes (7 words), texts (3 words and 24
    // bytes), then the header's four 8-byte strings. An empty path or text
    // array still holds one unused element, as in draw.hpp.
    this.nodesAt = Math.max(paths, 1) * (1 + 2 * points);
    this.textsAt = this.nodesAt + 7 * nodes.length;
    this.headerAt = this.textsAt + 9 * Math.max(texts.length, 1);
    this.frameWords = this.headerAt + 8;
    const ids = [...nodes, ...texts, ...palette];
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
    // The device renderer only changes a fill the artwork already has.
    this.fillable = this.nodes.map(node => (node.getAttribute('fill') || 'none') !== 'none');
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
    // Callers use PreviewDisplay, which runs the same native frame validator.
    const stride = 1 + 2 * this.points;
    for (let path = 0; path < this.paths.length; path++) {
      const offset = path * stride, key = frame[offset];
      if (this.keys[path] === key) continue;
      const segments = [];
      for (let i = 0; i < this.points; i++) {
        segments.push(`${i ? 'L' : 'M'}${decimal(frame[offset + 1 + i * 2])} ${decimal(frame[offset + 2 + i * 2])}`);
      }
      this.paths[path].setAttribute('d', segments.join(' ') + ' Z');
      this.keys[path] = key;
    }
    for (let i = 0; i < this.nodes.length; i++) {
      const offset = this.nodesAt + i * 7;
      const words = frame.subarray(offset, offset + 7), key = words.join();
      if (this.applied[i] === key) continue;
      this.applied[i] = key;
      const [x, y, scale, cos, sin, shade, paint] = words;
      const node = this.nodes[i], a = floorQ10(scale * cos), b = floorQ10(scale * sin);
      node.setAttribute('transform',
        `matrix(${decimal(a)} ${decimal(b)} ${decimal(-b)} ${decimal(a)} ${decimal(x)} ${decimal(y)})`);
      if (paint) {
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
  svg() { return new XMLSerializer().serializeToString(this.root); }
}
