// Shared control/patch plumbing. Descriptors are validated by the SDK generator.
const DIVISIONS = ['1/32', '1/32 dotted', '1/16', '1/16 dotted', '1/8', '1/8 dotted', '1/4', '1/2'];
function freeze(value) {
  if (value && typeof value === 'object') {
    for (const child of Object.values(value)) freeze(child);
    Object.freeze(value);
  }
  return value;
}

export function physicalValue(mapping, value) {
  if (mapping.kind === 'enum') return Math.floor((Math.round(value * 0x7fffffff) >>> 16) * mapping.labels.length / 32768);
  if (mapping.kind === 'linear') return mapping.minimum + (mapping.maximum - mapping.minimum) * value;
  if (mapping.kind === 'anchors') {
    const position = value * (mapping.values.length - 1);
    const i = Math.min(mapping.values.length - 2, Math.floor(position));
    return mapping.values[i] + (mapping.values[i + 1] - mapping.values[i]) * (position - i);
  }
  if (mapping.kind === 'power') return value ** mapping.exponent * mapping.maximum;
  if (mapping.kind === 'exponential') return mapping.minimum * (mapping.maximum / mapping.minimum) ** value;
  throw new Error('Unsupported parameter mapping');
}

// A synth's page position (encoder) for each parameter, in parameter order:
// the first four are the positions' own, later ones name theirs in `control`
// (the definition schema; tooling.definitions.control_positions). An absent
// parameter (an empty slot) belongs to none, null, unless a paged synth puts it
// on a position's second page.
export const controlPositions = parameters => parameters.map((p, i) => p.absent ? p.control ?? null : p.control ?? i);

// The parameters the four positions show after editing parameter `slot`
// (positions: controlPositions): that position shows it, or on a paged synth
// (`"press": "page"`) every position shows the edited parameter's page.
export function editedShown(shown, positions, slot, pages = false) {
  if (!pages) return shown.map((s, position) => position === positions[slot] ? slot : s);
  // A position with no alternate edits the same saved parameter on either page.
  // Turning that repeated control must not jump back to page 1.
  const shared = slot < 4 && !positions.slice(4).includes(slot);
  const second = shared ? shown.some(s => s >= 4) : slot >= 4;
  const next = [0, 1, 2, 3];
  positions.forEach((position, s) => { if (position !== null && (s >= 4) === second) next[position] = s; });
  return next;
}

// The display input (draw.hpp) for the eight parameter words in
// parameter order, when shown[i] is the parameter position i shows (initially
// its own; on the device an encoder press selects an alternate, in a preview
// the control last edited): each position's value, the shown slots as four
// bytes, and every slot's word.
export function shownControls(words, shown) {
  if (!Array.isArray(words) || words.length !== 8 || words.some(w => !Number.isInteger(w) || w < 0 || w > 0x7fffffff)) {
    throw new Error('Invalid parameter words');
  }
  if (!Array.isArray(shown) || shown.length !== 4 || shown.some(s => !Number.isInteger(s) || s < 0 || s >= words.length)) {
    throw new Error('Invalid shown controls');
  }
  return { parameters: shown.map(slot => words[slot]),
    shown: (shown[0] | shown[1] << 8 | shown[2] << 16 | shown[3] << 24) >>> 0, slots: [...words] };
}

// Checks a shownControls result and returns it as the input's words in
// layout order: parameters, the shown bytes, slots.
export function inputWords(input) {
  const { parameters, shown, slots } = input ?? {};
  const word = v => Number.isInteger(v) && v >= 0 && v <= 0x7fffffff;
  if (!Array.isArray(slots) || slots.length !== 8 || !slots.every(word) ||
      !Number.isInteger(shown) || shown < 0 || shown > 0x07070707 || shown & 0xf8f8f8f8 ||
      !Array.isArray(parameters) || parameters.length !== 4 ||
      parameters.some((v, i) => v !== slots[shown >>> 8 * i & 0xff])) throw new Error('Invalid display input');
  return [...parameters, shown, ...slots];
}

// A synth's Shift row entry (SDK 1.13) as the device stores and plays it: an amount in
// whole steps of 0..100, an enum's choice at the middle of its bucket
// (sdk/platform/opxy-1.2.1/synth_shift.hpp; tooling.definitions.shift_q31).
export function shiftWord(parameter, value) {
  if (parameter.absent) return 0;
  if (parameter.mapping.kind === 'enum') {
    const choices = parameter.mapping.labels.length, bucket = Math.floor(0x7fffffff / choices);
    const choice = Math.min(choices - 1, Math.floor((Math.floor(value * 0x7fffffff + 0.5) >>> 16) * choices / 32768));
    return choice * bucket + Math.floor(bucket / 2);
  }
  const field = Math.min(100, Math.floor(value * 100 + 0.5));
  return field === 100 ? 0x7fffffff : field * 21474836 + Math.floor((field * 47 + 50) / 100);
}

// Screen views of the browser preview.
export const VIEWS = Object.freeze(['routing', 'waveform']);

export function createControls(input) {
  const descriptor = freeze(input);
  const parameters = descriptor.parameters;
  const definition = freeze({ id: descriptor.id, name: descriptor.name });
  // A synth's Shift row (SDK 1.13): render's parameters 8..15. Its present entries are
  // controls too, which a value may leave out (they keep their defaults).
  const shift = descriptor.resources?.voiceBytes !== undefined ? descriptor.shift ?? [] : [];
  const shifted = shift.filter(p => !p.absent);
  const defaults = () => Object.fromEntries([...parameters, ...shifted].map(p => [p.id, p.default]));
  function validateParameters(value) {
    const valid = p => Object.hasOwn(value, p.id) && Number.isFinite(value[p.id]) && value[p.id] >= 0 && value[p.id] <= 1;
    if (!value || typeof value !== 'object' || Array.isArray(value) ||
        Object.keys(value).length !== parameters.length + shifted.filter(p => Object.hasOwn(value, p.id)).length ||
        parameters.some(p => !valid(p)) || shifted.some(p => Object.hasOwn(value, p.id) && !valid(p))) {
      throw new Error('Expected exactly the defined normalized controls between 0 and 1.');
    }
    return Object.fromEntries([...parameters.map(p => [p.id, value[p.id]]),
                               ...shifted.map(p => [p.id, Object.hasOwn(value, p.id) ? value[p.id] : p.default])]);
  }
  const physical = value => Object.fromEntries(parameters.map(p => [p.id, physicalValue(p.mapping, value[p.id])]));
  function displayValue(id, value) {
    const parameter = parameters.find(p => p.id === id) ?? shifted.find(p => p.id === id);
    if (!parameter) throw new Error('Unknown control');
    // A Shift row entry shows the value the device stores: whole steps, or a choice.
    if (!parameters.includes(parameter)) value = { [id]: shiftWord(parameter, value[id] ?? parameter.default) / 0x7fffffff };
    const v = physicalValue(parameter.mapping, value[id]);
    if (parameter.display === 'enum') return parameter.mapping.labels[v];
    if (parameter.display === 'ratio') return `${v.toFixed(1)}:1`;
    if (parameter.display === 'bipolar') return `${v > 0 ? '+' : ''}${Math.round(v * 100)}%`;
    if (parameter.display === 'semitones') return `${v > 0 ? '+' : ''}${v.toFixed(1)} st`;
    if (parameter.display === 'duration') return v < 1000 ? `${Math.round(v)} ms` : `${(v / 1000).toFixed(2)} s`;
    // Stock delay's note lengths, by the Q31 word >> 28 as on the device.
    if (parameter.display === 'division') return DIVISIONS[Math.round(value[id] * 0x7fffffff) >>> 28];
    return (value[id] * 100).toFixed(1);
  }
  function q31(value) {
    const all = validateParameters(value);
    const words = [...parameters.map(p => Math.round(value[p.id] * 0x7fffffff)), ...Array(8 - parameters.length).fill(0)];
    if (!shift.length) return words;
    return [...words, ...shift.map(p => p.absent ? 0 : shiftWord(p, all[p.id])), ...Array(8 - shift.length).fill(0)];
  }
  function makePatch(value, view = VIEWS[0]) {
    if (!VIEWS.includes(view)) throw new Error('Unknown screen view.');
    return { kind: 'opxy-machine-preview', machine: definition.id,
      parameters: validateParameters(value), view, parametersQ31: q31(value) };
  }
  function readPatch(patch) {
    if (!patch || patch.kind !== 'opxy-machine-preview' || patch.machine !== definition.id) {
      throw new Error('This is not a supported engine preview patch.');
    }
    const canonical = makePatch(patch.parameters, patch.view);
    if (JSON.stringify(patch.parametersQ31) !== JSON.stringify(canonical.parametersQ31)) {
      throw new Error('Q31 values do not match the normalized controls.');
    }
    return canonical;
  }
  // Presets name the present controls; an absent one (an empty slot) stays 0.
  // A preset holds no Shift row: loading one plays the row's defaults, as on the device.
  const absent = Object.fromEntries(parameters.filter(p => p.absent).map(p => [p.id, 0]));
  const rowDefaults = Object.fromEntries(shifted.map(p => [p.id, p.default]));
  const presets = descriptor.presets &&
    Object.fromEntries(Object.entries(descriptor.presets).map(([name, values]) => [name, { ...absent, ...rowDefaults, ...values }]));
  return Object.freeze({ definition, parameters, shift: freeze([...shift]), positions: Object.freeze(controlPositions(parameters)),
    defaults, presets, physical, displayValue, q31, validateParameters, makePatch, readPatch });
}
