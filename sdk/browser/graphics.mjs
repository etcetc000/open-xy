// Shared artwork selection for the installer and machine workbench.
const check = (ok, message) => { if (!ok) throw new Error(message); };

// Rewrite paint attributes without reserializing the XML: node IDs, path
// topology, transforms, text and numeric precision must reach stock unchanged.
// Palette donors live in this same artwork, so animation cannot restore colour.
function artworkPaint(artwork, visit, element = () => {}) {
  const elements = new Set(['svg', 'g', 'rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'text']);
  const tokens = /<!--[\s\S]*?-->|<\/?[a-zA-Z][^<>]*>|[^<]+/gy;
  const attributes = /\s+([\w:.-]+)\s*=\s*("[^"]*"|'[^']*')/gy;
  let at = 0, output = '', token, roots = 0;
  const stack = [];
  while ((token = tokens.exec(artwork))) {
    const source = token[0];
    at = tokens.lastIndex;
    if (!source.startsWith('<') || source.startsWith('<!--')) {
      check(stack.length || source.startsWith('<!--') || !source.trim(), 'Text outside SVG root');
      output += source; continue;
    }
    const tag = /^<(\/?)([a-z]+)([\s\S]*?)(\/?)>$/.exec(source);
    check(tag && elements.has(tag[2]), 'Unsupported SVG element in grayscale artwork');
    const [, closing, name, body, selfClosing] = tag;
    if (closing) {
      check(!body.trim() && !selfClosing, 'Invalid SVG closing tag');
      check(stack.pop()?.name === name, 'Mismatched SVG closing tag');
      output += source; continue;
    }
    let end = 0, rewritten = '', attribute;
    const values = {};
    attributes.lastIndex = 0;
    while ((attribute = attributes.exec(body))) {
      const [original, key, quoted] = attribute, value = quoted.slice(1, -1);
      check(!Object.hasOwn(values, key), 'Duplicate SVG attribute: ' + key);
      values[key] = value;
      check(!/^(?:style|color|filter|mask|clip-path|.*:.*)$/i.test(key) &&
        !/^on/i.test(key) && !/href/i.test(key), 'Unsupported SVG attribute in grayscale artwork: ' + key);
      rewritten += ['fill', 'stroke'].includes(key)
        ? original.slice(0, original.indexOf(quoted)) + quoted[0] + visit(value) + quoted[0] : original;
      end = attributes.lastIndex;
    }
    check(!body.slice(end).trim(), 'Invalid SVG attributes in grayscale artwork');
    if (!stack.length) roots++;
    const node = {name, values, ancestors:stack.map(parent => parent.values.transform ?? '')};
    element(node);
    if (!selfClosing) stack.push(node);
    output += '<' + name + rewritten + body.slice(end) + selfClosing + '>';
  }
  check(at === artwork.length && !stack.length && roots === 1, 'Invalid SVG in grayscale artwork');
  return output;
}

function paintRgb(value) {
  // The drawing contract uses #RRGGBB; also accept short hex and the basic
  // SVG colour keywords for unbound, static artwork. Unknown paint fails closed.
  const named = {black:'000000', silver:'c0c0c0', gray:'808080', grey:'808080', white:'ffffff',
    maroon:'800000', red:'ff0000', purple:'800080', fuchsia:'ff00ff', green:'008000',
    lime:'00ff00', olive:'808000', yellow:'ffff00', navy:'000080', blue:'0000ff', teal:'008080', aqua:'00ffff'};
  let hex = value.trim().toLowerCase();
  if (hex === 'none') return null;
  check(Object.hasOwn(named, hex) || /^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/.test(hex), 'Unsupported SVG paint: ' + value);
  hex = named[hex] ?? hex.slice(1);
  if (/^[0-9a-f]{3}$/.test(hex)) hex = [...hex].map(c => c + c).join('');
  check(/^[0-9a-f]{6}$/.test(hex), 'Unsupported SVG paint: ' + value);
  return hex.match(/../g).map(part => parseInt(part, 16));
}

function luminanceGray(rgb) {
  const linear = rgb.map(n => n / 255).map(n => n <= .04045 ? n / 12.92 : ((n + .055) / 1.055) ** 2.4);
  const luminance = linear[0] * .2126 + linear[1] * .7152 + linear[2] * .0722;
  return 255 * (luminance <= .0031308 ? 12.92 * luminance : 1.055 * luminance ** (1 / 2.4) - .055);
}

// A single SVG-wide palette, including animation donors. Hue families receive
// separate tonal bands; brightness is secondary within each band. This is a
// deterministic vector-palette heuristic, not a spatial image optimizer.
function grayscalePalette(colors) {
  const entries = [...colors.values()];
  const chromatic = entries.filter(entry => entry.chroma >= 48)
    .sort((a, b) => a.hue - b.hue || a.key.localeCompare(b.key));
  if (chromatic.length) {
    // Small palettes can afford to distinguish closer hues. Dense palettes
    // group nearby variations so they do not consume every available tone.
    const hueTolerance = chromatic.length <= 8 ? 12 : chromatic.length <= 24 ? 18 : 24;
    // Cut the hue circle at its largest empty arc, so reds around 0/360 stay
    // together. Sorting depends on colors, never SVG order or paint frequency.
    let start = 0, largest = -1;
    for (let i = 0; i < chromatic.length; i++) {
      const next = (i + 1) % chromatic.length;
      const gap = chromatic[next].hue + (next === 0 ? 360 : 0) - chromatic[i].hue;
      if (gap > largest) { largest = gap; start = next; }
    }
    const ordered = [...chromatic.slice(start), ...chromatic.slice(0, start)];
    const groups = [];
    for (const entry of ordered) {
      const hue = entry.hue < ordered[0].hue ? entry.hue + 360 : entry.hue;
      if (!groups.length || hue - groups.at(-1).hue > hueTolerance) groups.push({hue, entries:[]});
      groups.at(-1).entries.push(entry);
    }
    if (groups.length > 1) {
      // Spend the full usable range on the actual palette. A lone color needs
      // no band width. Extra distinct shades reserve a little room, capped at
      // a quarter of the range so hue separation always has priority. Do not
      // amplify tiny brightness differences into conspicuous shading steps.
      const low = 96, range = 152;
      const maxBandWidth = Math.min(32, range / (groups.length - 1) * .4);
      const bands = groups.map(group => {
        const shades = [...new Set(group.entries.map(entry => Math.round(entry.gray)))];
        const min = Math.min(...shades), max = Math.max(...shades);
        return {group, min, max, width:Math.min(maxBandWidth, (shades.length - 1) * 12, max - min)};
      });
      const requested = bands.reduce((sum, band) => sum + band.width, 0);
      const scale = requested ? Math.min(1, range * .25 / requested) : 1;
      const gap = (range - requested * scale) / (bands.length - 1);
      let start = low;
      for (const {group, min, max, width} of bands) {
        const span = width * scale;
        group.entries.forEach(entry => {
          const fraction = max > min ? Math.min(1, Math.max(0, (entry.gray - min) / (max - min))) : .5;
          entry.gray = start + fraction * span;
        });
        start += span + gap;
      }
    }
  }
  // Weak tints retain luminance rather than turning dark grids or near-neutral
  // labels into prominent accents. Exact neutral grays never enter this map.
  return new Map(entries.map(entry => {
    const channel = Math.round(entry.gray).toString(16).padStart(2, '0');
    return [entry.key, '#' + channel.repeat(3)];
  }));
}

export function artworkForGraphics(artwork, graphics = 'colorful') {
  check(['colorful', 'grayscale'].includes(graphics), 'Invalid machine graphics setting');
  if (graphics === 'colorful') return artwork;
  const colors = new Map();
  artworkPaint(artwork, value => {
    const rgb = paintRgb(value);
    if (rgb && (rgb[0] !== rgb[1] || rgb[1] !== rgb[2])) {
      const key = rgb.join(','), high = Math.max(...rgb), low = Math.min(...rgb), chroma = high - low;
      let hue = high === rgb[0] ? (rgb[1] - rgb[2]) / chroma
        : high === rgb[1] ? (rgb[2] - rgb[0]) / chroma + 2 : (rgb[0] - rgb[1]) / chroma + 4;
      hue = (hue * 60 + 360) % 360;
      colors.set(key, {key, hue, chroma, gray:luminanceGray(rgb)});
    }
    return value;
  });
  // Every paint (including animation donors) was inspected. An already-gray
  // screen keeps its original bytes, formatting and checksum exactly.
  if (!colors.size) return artwork;
  const palette = grayscalePalette(colors);
  const converted = artworkPaint(artwork, value => {
    const rgb = paintRgb(value);
    return rgb ? palette.get(rgb.join(',')) ?? value : value;
  });
  // Inspect the actual output, including off-screen palette donors and text.
  artworkPaint(converted, value => {
    const rgb = paintRgb(value);
    check(!rgb || rgb[0] === rgb[1] && rgb[1] === rgb[2], 'Packaged SVG is not grayscale');
    return value;
  });
  return converted;
}

export function validateGrayscaleArtwork(original, variant, bindings = null) {
  check(typeof variant === 'string' && variant.length > 0 && new TextEncoder().encode(variant).length <= 65536,
    'Invalid grayscale artwork size');
  check(artworkForGraphics(variant, 'grayscale') === variant, 'Developer grayscale artwork contains color');
  const collect = source => {
    const nodes = new Map();
    artworkPaint(source, value => value, node => {
      if (node.name !== 'g') check(node.values.fill !== undefined, 'Missing explicit SVG fill');
      if (node.name === 'text') check(node.values['font-family'] && node.values['font-size'], 'Missing explicit SVG text font');
      const numeric = new Set(['d', 'points', 'transform', 'x', 'y', 'x1', 'y1', 'x2', 'y2', 'cx', 'cy', 'r', 'rx', 'ry',
        'width', 'height', 'stroke-width', 'stroke-miterlimit', 'stroke-dasharray', 'stroke-dashoffset', 'font-size',
        'opacity', 'fill-opacity', 'stroke-opacity']);
      for (const [key, value] of Object.entries(node.values)) if (numeric.has(key))
        check(!/\d[eE][-+]?\d|\.\d{5,}/.test(value), 'Unsupported SVG numeric precision');
      const id = node.values.id;
      check(!['bg', 'bar', 'values', 'titles'].includes(id), 'Artwork shadows a platform binding: ' + id);
      if (id) { check(!nodes.has(id), 'Duplicate SVG ID: ' + id); nodes.set(id, node); }
    });
    return nodes;
  };
  const base = collect(original), gray = collect(variant);
  const ids = bindings ? [...bindings.nodes, ...bindings.texts, ...bindings.palette]
    : [...base.keys()].filter(id => /_(?:node|text|palette)_\d+$/.test(id));
  for (const id of ids) {
    const a = base.get(id), b = gray.get(id);
    check(a && b && a.name === b.name, 'Grayscale binding missing or changed type: ' + id);
    if (/_palette_\d+$/.test(id)) {
      check(['fill', 'stroke'].every(key => /^#[0-9a-f]{6}$/i.test(b.values[key])) &&
        Number(b.values['stroke-width']) > 0 && Number(b.values['stroke-width']) <= 16,
        'Invalid grayscale palette: ' + id);
      continue;
    }
    check((a.values.fill !== 'none') === (b.values.fill !== 'none') &&
      (a.values.transform ?? '') === (b.values.transform ?? '') &&
      JSON.stringify(a.ancestors) === JSON.stringify(b.ancestors), 'Grayscale binding origin or fill behavior changed: ' + id);
    if (bindings && bindings.nodes.indexOf(id) >= 0 && bindings.nodes.indexOf(id) < bindings.paths) {
      const points = b.values.d?.match(/[-+]?(?:\d+(?:\.\d*)?|\.\d+)/g) ?? [];
      check(b.values.fill === 'none' && b.values.d?.match(/[a-z]/gi)?.join('') === 'M' + 'L'.repeat(bindings.points - 1) + 'Z' &&
        points.length === bindings.points * 2 && new Set(Array.from({length:bindings.points}, (_, i) =>
          points.slice(i * 2, i * 2 + 2).map(n => Math.round(Number(n) * 1024)).join(','))).size === bindings.points,
        'Invalid grayscale mutable outline: ' + id);
    }
  }
  return variant;
}

export function selectArtwork(machine, graphics = 'colorful') {
  check(['colorful', 'grayscale'].includes(graphics), 'Invalid machine graphics setting');
  const original = machine.artwork, variant = machine.grayscaleArtwork;
  if (variant !== undefined) validateGrayscaleArtwork(original, variant, machine.preview?.graphics);
  if (graphics === 'colorful') return original;
  if (variant !== undefined) {
    let alreadyGray = true;
    artworkPaint(original, value => {
      // A valid developer variant need not depend on the automatic converter
      // understanding every color spelling used by the original artwork.
      try {
        const rgb = paintRgb(value);
        if (rgb && (rgb[0] !== rgb[1] || rgb[1] !== rgb[2])) alreadyGray = false;
      } catch { alreadyGray = false; }
      return value;
    });
    return alreadyGray ? original : variant;
  }
  return artworkForGraphics(original, 'grayscale');
}
