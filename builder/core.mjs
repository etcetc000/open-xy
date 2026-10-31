// Browser-only linker and container writer for a maintainer-curated kit.
// No compiler, device access, network requests, or arbitrary address input.
// Every firmware rule (edits, hooks, identity ranges, catalog offsets, table
// bytes) comes from the reviewed profile, authenticated by this digest. The
// Python SDK reads the same constant.
export const PROFILE_SHA256 = '291be838255b6b455dcabc148aa3cba423808e733092918780929f29deec086b';
export function requireThat(ok, message) { if (!ok) throw new Error(message); }
const check = requireThat;
const utf8 = new TextEncoder();
const decoder = new TextDecoder('utf-8', {fatal: true});
const align = (n, a) => Math.ceil(n / a) * a;
const hex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
const unhex = s => {
  check(typeof s === 'string' && /^(?:[a-f0-9]{2})+$/.test(s), 'Invalid hex bytes');
  return Uint8Array.from(s.match(/../g), b => parseInt(b, 16));
};
export const decode64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));
export const sha256 = async bytes => hex(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)));
export const equal = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
function view(bytes) { return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); }
function join(parts) {
  const output = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0; for (const p of parts) { output.set(p, at); at += p.length; } return output;
}

// Only ET_REL objects exported by the maintainer are accepted. SHF_MERGE,
// SHF_STRINGS and COMDAT groups are removed by the exporter, identically for GNU
// and this linker.
export function readObject(bytes) {
  check(bytes.length >= 52 && bytes.length <= 2 ** 20, 'Object size outside bounds');
  const d = view(bytes), u16 = at => d.getUint16(at, true), u32 = at => d.getUint32(at, true);
  check(hex(bytes.subarray(0, 6)) === '7f454c460101' && u16(16) === 1 && u16(18) === 106,
    'Expected a Blackfin relocatable object');
  const start = u32(32), count = u16(48), namesIndex = u16(50);
  check(u16(46) === 40 && count > 0 && count < 4096 && namesIndex < count && start + count * 40 <= bytes.length,
    'Invalid object section table');
  const sections = Array.from({length: count}, (_, index) => {
    const at = start + index * 40;
    const s = {index, nameOffset: u32(at), type: u32(at + 4), flags: u32(at + 8),
      offset: u32(at + 16), size: u32(at + 20), link: u32(at + 24), info: u32(at + 28),
      alignment: u32(at + 32) || 1, stride: u32(at + 36)};
    check(s.size <= 2 ** 20 && s.alignment <= 4096 && (s.alignment & (s.alignment - 1)) === 0,
      'Invalid object section size/alignment');
    check(s.type === 8 || s.offset + s.size <= bytes.length, 'Object section exceeds file');
    s.data = s.type === 8 ? new Uint8Array() : bytes.subarray(s.offset, s.offset + s.size);
    return s;
  });
  const string = (table, at) => {
    check(at < table.length, 'Invalid object string offset');
    const end = table.indexOf(0, at); check(end >= at, 'Unterminated object string');
    return decoder.decode(table.subarray(at, end));
  };
  const groups = ['text', 'rodata', 'data', 'bss'];
  for (const s of sections) {
    s.name = string(sections[namesIndex].data, s.nameOffset);
    if (!(s.flags & 2)) continue;
    check(!(s.flags & ~7), 'Unsupported allocated section flags');
    const n = s.name;
    s.group = /^(\.rodata(?:\.|$)|\.data\.rel\.ro(?:\.|$))/.test(n) ? 'rodata'
      : /^(\.data(?:\.|$)|\.sdata(?:\.|$))/.test(n) ? 'data'
      : /^(\.bss(?:\.|$)|\.sbss(?:\.|$))/.test(n) ? 'bss'
      : /^\.text(?:\.|$)/.test(n) ? 'text' : null;
    check(groups.includes(s.group) && s.type === (s.group === 'bss' ? 8 : 1),
      'Unsupported allocated section: ' + n);
  }
  const tables = sections.filter(s => s.type === 2);
  check(tables.length === 1, 'Expected one symbol table');
  const table = tables[0];
  check(table.stride === 16 && table.size % 16 === 0 && table.link < count, 'Invalid symbol table');
  const symbols = Array.from({length: table.size / 16}, (_, i) => {
    const at = table.offset + i * 16, info = bytes[at + 12], section = u16(at + 14);
    check(section < count || section === 0xfff1, 'COMMON and extended symbol sections are unsupported');
    return {name: string(sections[table.link].data, u32(at)), value: u32(at + 4), size: u32(at + 8),
      binding: info >> 4, type: info & 15, section};
  });
  const relocations = [], touched = new Set();
  for (const s of sections) {
    check(s.type !== 9, 'REL without explicit addends is unsupported');
    if (s.type !== 4) continue;
    check(s.link === table.index && s.info < count && s.stride === 12 && s.size % 12 === 0,
      'Invalid relocation table');
    check(sections[s.info].group && sections[s.info].type !== 8, 'Relocation target is not initialized memory');
    for (let at = s.offset; at < s.offset + s.size; at += 12) {
      const info = u32(at + 4), type = info & 255, symbol = info >>> 8;
      check([6, 7, 10, 13, 18].includes(type) && symbol < symbols.length, 'Unsupported relocation');
      const offset = u32(at), size = type === 18 ? 4 : 2;
      check(offset % 2 === 0 && offset + size <= sections[s.info].size &&
        (![10, 13].includes(type) || offset >= 2), 'Relocation exceeds section');
      for (let i = [10, 13].includes(type) ? offset - 2 : offset; i < offset + size; i++) {
        const key = s.info + ':' + i;
        check(!touched.has(key), 'Overlapping relocations'); touched.add(key);
      }
      relocations.push({section: s.info, offset, type, symbol, addend: d.getInt32(at + 8, true)});
    }
  }
  return {sections, symbols, relocations};
}

// The selections the release checks link, as BigInt bit masks over kit.machines (a
// kit holds more machines than a 32-bit mask): every combination of Open-XY's own
// machines, each other machine with all of those, and the machines the native package
// links (kit.linked; all of them when they fit).
export const selectionBit = i => 1n << BigInt(i);
export function checkedSelections(machines, linked = machines.map(m => m.id)) {
  const core = machines.reduce((mask, m, i) => m.origin === 'official' ? mask | selectionBit(i) : mask, 0n);
  const masks = new Set([machines.reduce((mask, m, i) => linked.includes(m.id) ? mask | selectionBit(i) : mask, 0n)]);
  for (let mask = core; mask; mask = (mask - 1n) & core) masks.add(mask);
  machines.forEach((m, i) => { if (!(core & selectionBit(i))) masks.add(core | selectionBit(i)); });
  return [...masks].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
}

// The profile's identity rules, as tooling.machines.check_identity applies them.
export function validIdentity(rules, kind, identity) {
  const slot = kind === 'effect' ? 'effect' : 'engine';
  if (!identity || typeof identity !== 'object' || Object.keys(identity).sort().join() !== [slot, 'page'].sort().join()) return false;
  const n = identity[slot], page = identity.page;
  if (!Number.isInteger(n) || !Number.isInteger(page)) return false;
  if (kind === 'effect') return rules.effects.includes(n) && page === rules.effect_page;
  if (!(n > 0 && n < rules.engines && n !== rules.engine_none && !rules.stock_engines.includes(n))) return false;
  return kind === 'sample' ? n > rules.engine_none && rules.sample_pages.includes(page)
    : kind === 'synth' && page > rules.page_none && page < rules.pages;
}

// The numbers a kit machine asks for (tooling.machines.check_request): valid ones,
// or from SDK 1.5 null for each the layout gives. A sample engine still names its stock
// page (its base) and an effect the FX page.
export function validRequest(rules, kind, identity) {
  if (!identity || typeof identity !== 'object') return false;
  const slot = kind === 'effect' ? 'effect' : 'engine';
  if (Object.keys(identity).sort().join() !== [slot, 'page'].sort().join() || identity[slot] !== null)
    return validIdentity(rules, kind, identity);
  return kind === 'effect' ? identity.page === rules.effect_page
    : kind === 'sample' ? rules.sample_pages.includes(identity.page) : kind === 'synth' && identity.page === null;
}
// Whether numbers are what a machine asks for: the same fields, equal where it names one.
const fits = (requested, numbers) => Object.keys(requested).length === Object.keys(numbers).length &&
  Object.keys(requested).every(k => requested[k] === null || requested[k] === numbers[k]);
// A kit's numbers: each machine asks for valid ones, and fixed numbers (kit.fixed,
// tooling.machines.FIXED_NUMBERS) belong to their own machines and are never shared.
function checkNumbers(kit) {
  const rules = kit.profile.identities;
  check(kit.fixed && typeof kit.fixed === 'object', 'Missing fixed machine numbers');
  check(new Set(kit.machines.map(m => m.id)).size === kit.machines.length, 'Duplicate machine');
  for (const m of kit.machines) {
    check(validRequest(rules, m.kind, m.identity), 'Machine numbers overlap a stock slot or exceed capacity: ' + m.id);
    check(!Object.hasOwn(kit.fixed, m.id) || fits(m.identity, kit.fixed[m.id]),
      'Fixed machine numbers differ: ' + m.id);
  }
  numberLayout(rules, Object.entries(kit.fixed).map(([id, identity]) => [id, null, identity]), null, []);
}
// Machine numbers, as tooling.machines.number_layout gives them (tests/fixtures/numbers.json
// holds both). Projects store a track's engine, effect and page numbers, so a machine
// keeps its numbers in a firmware for good. Open-XY's first machines' are fixed (kit.fixed);
// a layout keeps every number it has given out, including to machines since dropped,
// and machines new to it get the numbers they ask for when free (only machines before
// SDK 1.5 ask for any), then each number still missing from the machine's own place in
// the pool (preferred), in ID order, so numbers never depend on the selection's order.
export const LAYOUT_FORMAT = 'opxy-layout/1';
// FNV-1a, 32-bit, of the text's UTF-8 bytes (tooling.machines.fnv1a).
export function fnv1a(text) {
  let value = 0x811c9dc5;
  for (const byte of new TextEncoder().encode(text)) value = Math.imul(value ^ byte, 0x01000193) >>> 0;
  return value;
}
// A pool's numbers in the order a machine new to a layout takes the first free one: from
// FNV-1a of its ID modulo the pool's size, upward and wrapping around.
export function preferred(pool, name) {
  const start = fnv1a(name) % pool.length;
  return [...pool.slice(start), ...pool.slice(0, start)];
}
export function numberPools(rules) {
  const range = (low, high) => Array.from({length: high - low}, (_, i) => low + i);
  return {engine: range(rules.engine_none + 1, rules.engines).filter(n => !rules.stock_engines.includes(n)),
    page: range(rules.page_none + 1, rules.pages), effect: [...rules.effects]};
}
export function occupied(rules, identity) {
  const keys = ['engine', 'effect'].filter(slot => Object.hasOwn(identity, slot)).map(slot => slot + ' ' + identity[slot]);
  // Stock reads a send track's effect number as its engine: no engine shares it.
  if (Object.hasOwn(identity, 'effect')) keys.push('engine ' + identity.effect);
  if (!Object.hasOwn(identity, 'effect') && !rules.sample_pages.includes(identity.page)) keys.push('page ' + identity.page);
  return keys;
}
const sameNumbers = (a, b) => Object.keys(a).length === Object.keys(b).length && Object.keys(a).every(k => a[k] === b[k]);
// fixed and machines are [id, kind, identity] (requested, null where a machine asks for
// none, for machines); returns the layout after.
export function numberLayout(rules, fixed, layout, machines) {
  const pools = numberPools(rules), numbers = {}, holders = new Map();
  const hold = (name, identity) => {
    for (const key of occupied(rules, identity)) {
      if (!holders.has(key)) holders.set(key, name);
      check(holders.get(key) === name, `${name} and ${holders.get(key)} both hold ${key} in this layout`);
    }
    numbers[name] = {...identity};
  };
  for (const [name, , identity] of fixed) hold(name, identity);
  for (const [name, identity] of Object.entries(layout ?? {})) {
    if (Object.hasOwn(numbers, name))
      check(sameNumbers(numbers[name], identity), `${name} has fixed numbers; the layout gives it others`);
    // An effect number later found unusable on stock is let go and numbered again.
    else if (Object.hasOwn(identity, 'effect') && !pools.effect.includes(identity.effect)) continue;
    else hold(name, identity);
  }
  const fresh = [];
  for (const [name, kind, requested] of [...machines].sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)) {
    if (Object.hasOwn(numbers, name)) {
      check(validIdentity(rules, kind, numbers[name]), `${name}: the layout gives it numbers it cannot use`);
      check(kind !== 'sample' || numbers[name].page === requested.page,
        `${name} now forks another stock sampler; the layout holds its numbers for the old one`);
    } else {
      fresh.push([name, kind, {...requested}, kind === 'effect' ? ['effect'] : kind === 'synth' ? ['engine', 'page'] : ['engine']]);
    }
  }
  // An effect number is also held as an engine number (occupied).
  const keys = (slot, n) => [slot + ' ' + n, ...(slot === 'effect' ? ['engine ' + n] : [])];
  const free = (slot, n) => !keys(slot, n).some(key => holders.has(key));
  const take = (name, identity, slot, n) => {
    identity[slot] = n;
    for (const key of keys(slot, n)) holders.set(key, name);
  };
  // Requested numbers first, so no machine's preferred place takes one another asks for.
  for (const [name, , identity, slots] of fresh) {
    for (const slot of slots) {
      const n = identity[slot];
      identity[slot] = null;
      if (pools[slot].includes(n) && free(slot, n)) take(name, identity, slot, n);
    }
  }
  for (const [name, kind, identity, slots] of fresh) {
    for (const slot of slots) {
      if (identity[slot] !== null) continue;
      const n = preferred(pools[slot], name).find(n => free(slot, n));
      check(n !== undefined, `No free ${slot} number for ${name}: every one is held in this layout`);
      take(name, identity, slot, n);
    }
    check(validIdentity(rules, kind, identity), `${name}: no valid numbers`);
    hold(name, identity);
  }
  return numbers;
}
// A saved layout (layout.json from a build, or a saved selection): its numbers and selection.
// Saved selections from before layouts listed the selected machines' numbers as identities.
export function readLayout(kit, saved) {
  // Earlier profiles with the same identity rules number machines the same way.
  check(saved && typeof saved === 'object' &&
    [kit.profile.id, ...(kit.profile.layout_profiles ?? [])].includes(saved.profile),
    'This layout is for a different OS version.');
  const numbers = saved.format === LAYOUT_FORMAT ? saved.machines : saved.format === 'opxy-kit/1' ? saved.identities : null;
  check(numbers && typeof numbers === 'object' && !Array.isArray(numbers), 'This file is not a machine layout.');
  for (const [name, identity] of Object.entries(numbers))
    check(/^[a-z][a-z0-9_]{0,14}$/.test(name) && identity && typeof identity === 'object' &&
      [['engine', 'page'], ['effect', 'page']].some(keys => Object.keys(identity).sort().join() === keys.sort().join()) &&
      Object.values(identity).every(Number.isInteger), 'Invalid numbers in this layout: ' + name);
  const selected = saved.selected ?? null;
  check(selected === null || Array.isArray(selected) && selected.every(id => kit.machines.some(m => m.id === id)),
    'This layout lists a machine this site doesn’t have.');
  return {machines: numbers, selected};
}
// The kit's machines numbered for this selection, in kit order, and the layout after.
export function assignNumbers(kit, selection, layout = null) {
  return numberLayout(kit.profile.identities, Object.entries(kit.fixed).map(([id, identity]) => [id, null, identity]),
    layout, kit.machines.filter(m => selection.includes(m.id)).map(m => [m.id, m.kind, m.identity]));
}
// Each machine's link-time numbers: its own when selected, else 0, which no
// startup, catalog or control lookup matches.
function numberSymbols(kit, selection, numbers) {
  const symbols = {};
  for (const m of kit.machines) {
    const own = selection.includes(m.id) ? numbers[m.id] : null;
    if (m.kind === 'effect') Object.assign(symbols, {[m.prefix + '_fx_id']: own?.effect ?? 0, [m.prefix + '_fx_page_id']: own?.page ?? 0});
    else Object.assign(symbols, {[m.prefix + '_engine_id']: own?.engine ?? 0, [m.prefix + '_page_id']: own?.page ?? 0});
  }
  return symbols;
}
// A machine's stock menu entry; an effect's carries its effect number.
function catalogEntry(m, numbers) {
  return m.kind === 'effect' ? m.catalog.replace(/"id":\s*\d+/, '"id": ' + numbers[m.id].effect) : m.catalog;
}

// Where a machine comes from (tooling.browser_kit): published in the catalog, or a
// personal kit's own folder. A catalog machine's source is its
// GitHub repository, release tag, commit and maintainer, matching tooling.catalog's patterns.
// Official catalog machines are Open-XY's own (tooling.catalog.OFFICIAL); the rest are community.
export const ORIGINS = ['official', 'catalog', 'personal'];
// Extensions come from the same places as machines (tooling.browser_kit.extension_origins).
export const EXTENSION_ORIGINS = ORIGINS;
const RELEASE = {repository: /^https:\/\/github\.com\/[A-Za-z0-9-]+\/(?!\.\.?$)[A-Za-z0-9._-]+$/,
  tag: /^[A-Za-z0-9._/-]{1,64}$/, commit: /^[0-9a-f]{40}$/, maintainer: /^[A-Za-z0-9-]{1,39}$/};
export function validSource(source) {
  return source !== null && typeof source === 'object' &&
    Object.keys(source).sort().join() === Object.keys(RELEASE).sort().join() &&
    Object.entries(RELEASE).every(([key, pattern]) => typeof source[key] === 'string' && pattern.test(source[key]));
}
const released = m => m.origin === 'catalog' || m.origin === 'official';
export const validOrigin = m => ORIGINS.includes(m.origin) && (released(m) ? validSource(m.source) : m.source === undefined);
// A first selection leaves community machines for the user to choose.
export const chosenByDefault = m => m.origin !== 'catalog';
// The machine's code at the commit the catalog pins, and its owner/repository name.
export const sourceUrl = source => source.repository + '/tree/' + source.commit;
export const sourceName = source => source.repository.slice('https://github.com/'.length);

// Validate choices and retain the SDK's declared callback order.
export function extensionSelection(kit, selection = (kit.extensions ?? []).map(e => e.id)) {
  const entries = kit.extensions ?? [];
  check(Array.isArray(selection) && new Set(selection).size === selection.length &&
    selection.every(id => entries.some(e => e.id === id)), 'Choose each available extension once.');
  const selected = entries.filter(e => selection.includes(e.id));
  const shared = sharedClaim(selected);
  check(!shared, shared && `These extensions both use ${claimLabel(selected, shared)}. Choose only one.`);
  return selected.map(e => e.id); // Collection order, never checkbox order.
}

// A claim (an exclusive control such as Record double-tap) two extensions share, if any.
export function sharedClaim(extensions) {
  const seen = new Set();
  for (const e of extensions) for (const claim of e.claims ?? []) {
    if (seen.has(claim)) return claim;
    seen.add(claim);
  }
  return null;
}

// The registry's label for a claim, as the kit carries it with each extension.
export const claimLabel = (extensions, claim) =>
  extensions.map(e => e.claim_labels?.[claim]).find(Boolean) ?? claim.replaceAll('_', ' ');

// Every extension choice a build can make, in collection order: each subset
// in which no two extensions share a claim. Recovery and release checks try each.
export function extensionChoices(kit) {
  const entries = kit.extensions ?? [], choices = [];
  for (let mask = 0; mask < 2 ** entries.length; mask++) {
    const chosen = entries.filter((_, i) => mask & 2 ** i);
    if (!sharedClaim(chosen)) choices.push(chosen.map(e => e.id));
  }
  return choices;
}

// Memory every machine and extension shares, below the relocated stock tables.
export const moduleBytes = profile => profile.reserved_bytes - profile.table_bytes;
const screenTemplateOk = async kit => typeof kit.screenTemplate === 'string' &&
  await sha256(utf8.encode(kit.screenTemplate)) === kit.profile.screen_template_sha256;
export async function openKit(kit) {
  check(kit.format === 'opxy-browser-kit/1', 'Unsupported curated kit');
  // The release version, which every build's boot label shows (tooling.startup_label).
  check(typeof kit.version === 'string' && /^\d+\.\d+\.\d+$/.test(kit.version), 'Unsupported curated kit version');
  const profileBytes = decode64(kit.profileSource);
  check(await sha256(profileBytes) === PROFILE_SHA256 &&
    JSON.stringify(JSON.parse(decoder.decode(profileBytes))) === JSON.stringify(kit.profile),
    'Firmware edit profile is not the reviewed profile');
  // No more machines than the relocated engine table holds (tooling/stock_tables.py).
  check(kit.machines.length > 0 && kit.machines.length <= kit.profile.identities.engines, 'Unsupported curated kit');
  checkNumbers(kit);
  check(kit.machines.every(validOrigin), 'Invalid machine origin or source');
  const extensions = kit.extensions ?? [];
  check(Array.isArray(extensions) && extensions.length <= 16, 'Invalid extension collection');
  check(!kit.objects.some(o => o.name === 'sdk/extension_collection.o') || Array.isArray(kit.extensions),
    'This kit predates extension selection. Export a new kit to choose extensions.');
  const extensionIds = new Set();
  for (const e of extensions) {
    check(e && /^[a-z][a-z0-9_]{0,31}$/.test(e.id) && !extensionIds.has(e.id) &&
      Object.entries({name: 32, description: 140, note: 100}).every(([key, limit]) => key === 'note' && e[key] === undefined ||
        typeof e[key] === 'string' && e[key].length > 0 && e[key].length <= limit) &&
      EXTENSION_ORIGINS.includes(e.origin) &&
      e.claim_labels && typeof e.claim_labels === 'object' &&
      Object.keys(e.claim_labels).sort().join() === [...(e.claims ?? [])].sort().join() &&
      Object.values(e.claim_labels).every(l => typeof l === 'string' && l.length > 0 && l.length <= 40) &&
      ['claims', 'actions'].every(key => Array.isArray(e[key]) && e[key].length <= 8 &&
        new Set(e[key]).size === e[key].length && e[key].every(n => /^[a-z][a-z0-9_]{0,31}$/.test(n))),
      'Invalid extension metadata');
    extensionIds.add(e.id);
  }
  check(kit.linked === undefined || (Array.isArray(kit.linked) && kit.linked.length > 0 &&
    new Set(kit.linked).size === kit.linked.length && kit.linked.every(id => kit.machines.some(m => m.id === id))),
    'Invalid linked machines');
  // The bundled stock menu is authenticated where it is used (buildAssets).
  check(typeof kit.stockCatalog === 'string' && kit.stockCatalog.length <= 2 * 1024 ** 2, 'Missing bundled stock menu');
  check(await screenTemplateOk(kit), 'Platform screen template differs from the supported ABI');
  const ids = new Set(kit.machines.map(m => m.id)), names = new Set();
  const objects = [];
  for (const o of kit.objects) {
    check(!names.has(o.name) && (o.owner === null || ids.has(o.owner)), 'Duplicate or unowned object');
    check(o.service === undefined || (o.owner === null && Object.hasOwn(kit.profile.services ?? {}, o.service)),
      'Undeclared service: ' + o.service);
    check(o.extension === undefined || (o.owner === null && o.service === undefined && extensionIds.has(o.extension)),
      'Undeclared extension object');
    names.add(o.name);
    const bytes = decode64(o.bytes);
    check(await sha256(bytes) === o.sha256, 'Curated object hash mismatch: ' + o.name);
    objects.push({...o, ...readObject(bytes)});
  }
  for (const id of extensionIds) {
    const owned = objects.filter(o => o.extension === id);
    check(owned.length === 1 && owned[0].symbols.some(s => s.section && s.binding === 1 &&
      s.name === 'sdk_extension_' + id + '_hooks'), 'Missing extension accessor: ' + id);
  }
  return {...kit, objects};
}

// Profile services a selection needs: each names the machine kinds that use it,
// and 'extension' for any selected extension. tooling.firmware.active_services
// applies the same rule.
export function activeServices(profile, machines) {
  return new Set(Object.entries(profile.services ?? {})
    .filter(([, kinds]) => machines.some(m => kinds.includes(m.kind))).map(([name]) => name));
}

// Extensions are linked only when named: omitting them builds none.
export function linkKit(kit, selection, layout = null, extensionIds = []) {
  const extensions = new Set(extensionSelection(kit, extensionIds));
  check(Array.isArray(selection) && selection.length > 0 && new Set(selection).size === selection.length &&
    selection.every(id => kit.machines.some(m => m.id === id)), 'Choose each machine once.');
  const numbers = assignNumbers(kit, selection, layout);
  const definitions = {...kit.definitions, ...numberSymbols(kit, selection, numbers)};
  const selected = new Set(selection), limit = moduleBytes(kit.profile);
  const services = activeServices(kit.profile, [...kit.machines.filter(m => selected.has(m.id)),
    ...(extensions.size ? [{kind: 'extension'}] : [])]);
  const objects = kit.objects.filter(o => (o.extension === undefined || extensions.has(o.extension)) &&
    (o.owner === null ? !o.service || services.has(o.service) : selected.has(o.owner)));
  const globals = new Map(Object.entries(definitions)), placements = new Map(), segments = [];
  let cursor = kit.profile.module_base, initializedEnd;
  for (const group of ['text', 'rodata', 'data', 'bss']) {
    const all = objects.flatMap(o => o.sections.filter(s => s.group === group).map(s => ({o, s})));
    cursor = align(cursor, Math.max(group === 'text' ? 1 : 4, ...all.map(({s}) => s.alignment)));
    for (const {o, s} of all) {
      cursor = align(cursor, s.alignment);
      placements.set(o.name + ':' + s.index, cursor);
      if (s.size) segments.push({owner: o.owner, name: s.name, group, address: cursor, size: s.size});
      cursor += s.size;
      if (cursor > kit.profile.module_base + limit) {
        // One pool for every machine's code, data and state: say who uses it.
        const used = new Map();
        for (const x of objects) for (const y of x.sections) if (y.group) used.set(x.owner ?? 'sdk', (used.get(x.owner ?? 'sdk') || 0) + y.size);
        const shares = [...used].sort((a, b) => b[1] - a[1]).map(([owner, size]) => `${owner} ${(size / 1024).toFixed(1)} KiB`);
        check(false, `These machines need more than the ${limit / 1024} KiB available: ${shares.join(', ')}`);
      }
    }
    if (group === 'data') initializedEnd = cursor;
  }
  const allocated = cursor - kit.profile.module_base, initialized = initializedEnd - kit.profile.module_base;
  const bssStart = Math.min(cursor, ...segments.filter(s => s.group === 'bss').map(s => s.address));
  check(initialized > 0, 'Empty module');
  const definedValue = (o, s) => {
    if (s.section === 0xfff1) return s.value;
    const base = placements.get(o.name + ':' + s.section);
    check(base !== undefined && s.value + s.size <= o.sections[s.section].size, 'Symbol outside allocated section: ' + s.name);
    return base + s.value;
  };
  for (const o of objects) for (const s of o.symbols) {
    if (!s.section || !s.binding || !s.name || s.type === 4) continue;
    check(s.binding === 1, 'Weak/global binding is unsupported');
    check(!globals.has(s.name), 'Duplicate global symbol: ' + s.name);
    globals.set(s.name, definedValue(o, s));
  }
  // SDK objects may import the declared callbacks of unselected machines;
  // those resolve to the declared fallbacks. Machine objects never do.
  const aliases = new Map();
  for (const e of kit.extensions ?? []) if (!extensions.has(e.id)) {
    const symbol = 'sdk_extension_' + e.id + '_hooks';
    check(!globals.has(symbol) && globals.has('kit_absent'), 'Invalid absent-extension accessor');
    aliases.set(symbol, globals.get('kit_absent'));
  }
  for (const m of kit.machines) if (!selected.has(m.id)) {
    for (const [symbol, fallback] of Object.entries(m.absent)) {
      check(!globals.has(symbol) && globals.has(fallback), 'Invalid absent-machine callback');
      aliases.set(symbol, globals.get(fallback));
    }
  }
  const module = new Uint8Array(initialized), out = view(module);
  for (const o of objects) {
    for (const s of o.sections) if (s.group && s.type !== 8 && s.size) {
      module.set(s.data, placements.get(o.name + ':' + s.index) - kit.profile.module_base);
    }
    for (const r of o.relocations) {
      const sym = o.symbols[r.symbol], address = placements.get(o.name + ':' + r.section) + r.offset;
      const S = sym.section ? definedValue(o, sym) : globals.get(sym.name) ??
        (o.owner === null && o.extension === undefined ? aliases.get(sym.name) : undefined);
      check(S !== undefined, 'Unresolved import: ' + sym.name);
      const value = S + r.addend, offset = address - kit.profile.module_base;
      check(Number.isSafeInteger(value) && value >= 0 && value <= 0xffffffff, 'Symbol arithmetic overflow');
      if (r.type === 18) out.setUint32(offset, value, true);
      else if (r.type === 6) out.setUint16(offset, value & 0xffff, true);
      else if (r.type === 7) out.setUint16(offset, Math.floor(value / 65536), true);
      else {
        const opcode = out.getUint16(offset - 2, true), delta = value - (address - 2);
        check((opcode & 0xff00) === (r.type === 10 ? 0xe300 : 0xe200) &&
          delta % 2 === 0 && delta >= -(2 ** 24) && delta < 2 ** 24, 'Invalid or out-of-range relative branch');
        const encoded = delta / 2 & 0xffffff;
        out.setUint16(offset - 2, (opcode & 0xff00) | encoded >>> 16, true);
        out.setUint16(offset, encoded & 0xffff, true);
      }
    }
  }
  return {module, globals, aliases, objects, segments, services, extensions: [...extensions], numbers, definitions,
    sizes: {initialized, bss: cursor - bssStart, allocated, available: limit - allocated}};
}

export function readLdr(bytes) {
  check(bytes.length > 0 && bytes.length <= 16 * 1024 ** 2, 'LDR size outside bounds');
  const d = view(bytes), blocks = []; let at = 0;
  while (at < bytes.length) {
    check(blocks.length < 10000 && at + 16 <= bytes.length, 'Invalid loader record');
    const code = d.getUint32(at, true), target = d.getUint32(at + 4, true), count = d.getUint32(at + 8, true);
    check(code >>> 24 === 0xad && bytes.subarray(at, at + 16).reduce((a, b) => a ^ b, 0) === 0,
      'Invalid loader signature/checksum');
    const fill = Boolean(code & 256), stored = fill ? 0 : count;
    check(target + count <= 2 ** 32 && at + 16 + stored <= bytes.length, 'Loader range overflow');
    blocks.push({offset: at, code, target, count, fill, argument: d.getUint32(at + 12, true)});
    at += 16 + stored;
    if (code & 0x8000) { check(at === bytes.length, 'Trailing bytes after FINAL'); return blocks; }
  }
  throw new Error('Missing FINAL record');
}
function header(flags, target, count, argument) {
  const bytes = new Uint8Array(16), d = view(bytes);
  [0xad000000 | flags, target, count, argument].forEach((v, i) => d.setUint32(i * 4, v, true));
  bytes[2] = bytes.reduce((a, b) => a ^ b, 0); return bytes;
}
const words = (...values) => {
  const bytes = new Uint8Array(values.length * 2);
  values.forEach((word, i) => view(bytes).setUint16(i * 2, word, true)); return bytes;
};
// Replacement bytes of a profile edit whose destination is a module symbol
// (tooling.firmware.hook_instruction and tooling.startup_label.jump).
export function redirect(edit, destination) {
  check(Number.isInteger(destination) && destination >= 0 && destination <= 0xffffffff && destination % 2 === 0,
    'Invalid redirect destination');
  if (edit.kind === 'startup') {
    check(edit.length === 10, 'Startup label must occupy ten bytes');
    return words(0xdc00, destination >>> 16, destination & 0xffff, 0, 0);
  }
  check(edit.kind === 'hook' && typeof edit.call === 'boolean', 'Not a hook');
  if (edit.length === 8 || edit.length === 12) {
    check(edit.call, 'Wide hook must be a call');
    const after = new Uint8Array(edit.length);
    after.set(words(0xde00, destination >>> 16, destination & 0xffff, 0));
    return after;
  }
  const delta = destination - Number(edit.address);
  check([4, 6].includes(edit.length) && delta % 2 === 0 && delta >= -(2 ** 24) && delta < 2 ** 24, 'Hook branch out of range');
  const value = delta / 2 & 0xffffff, after = new Uint8Array(edit.length);
  after.set(words((edit.call ? 0xe300 : 0xe200) | value >>> 16, value & 0xffff));
  return after;
}
export async function patchFirmware(kit, linked, stock, {profiling = true} = {}) {
  check(typeof profiling === 'boolean', 'Profiling must be a boolean');
  const p = kit.profile;
  check(stock.length === p.stock_bytes && await sha256(stock) === p.stock_sha256,
    'Only OP–XY OS 1.2.1 is supported. Choose the OS file, os.ldr, copied from your OP–XY, not a .tfw update file.');
  const blocks = readLdr(stock), first = blocks[0], final = blocks.at(-1);
  check(blocks.length === p.stock_records && first.count === 0 && (first.code & 0xffff) === 0x5004 &&
    final.count === 0 && (final.code & 0xffff) === 0x8004, 'Unexpected stock loader layout');
  const low = p.module_base, high = low + p.reserved_bytes;
  const overlaps = blocks.filter(b => b.target < high && low < b.target + b.count);
  check(overlaps.length === 1 && overlaps[0].fill && overlaps[0].argument === 0 &&
    overlaps[0].target === p.heap_fill_base && overlaps[0].count === p.heap_fill_bytes &&
    overlaps[0].target <= low && overlaps[0].target + overlaps[0].count >= high,
    'Reservation is not covered by the unchanged stock zero-fill');
  check(linked.module.length > 0 && linked.sizes.allocated <= moduleBytes(p), 'Module exceeds its profile');
  const patched = stock.slice(), touched = new Set();
  for (const edit of p.patches) {
    const site = Number(edit.address), offset = Number(edit.file_offset), before = unhex(edit.before);
    const covering = blocks.filter(b => b.target < site + before.length && site < b.target + b.count);
    check(covering.length === 1 && !covering[0].fill && covering[0].target <= site &&
      site + before.length <= covering[0].target + covering[0].count &&
      covering[0].offset + 16 + site - covering[0].target === offset &&
      equal(stock.subarray(offset, offset + before.length), before), 'Patch preimage/mapping mismatch');
    let after;
    check(edit.service === undefined || (edit.kind === 'hook' && Object.hasOwn(p.services ?? {}, edit.service)),
      'Undeclared service: ' + edit.service);
    check(edit.profiling === undefined || (edit.profiling === false && edit.kind === 'hook'),
      'Invalid profiling hook');
    if ((edit.service && !linked.services.has(edit.service)) || (edit.profiling === false && profiling)) {
      after = before;  // inactive hooks leave the corresponding stock code unchanged
    } else if (['hook', 'startup'].includes(edit.kind)) {
      const destination = linked.globals.get(edit.symbol);
      check(destination >= low && destination < low + linked.module.length, 'Hook destination outside module');
      after = redirect(edit, destination);
    } else {
      check(['heap', 'table', 'text'].includes(edit.kind), 'Unknown edit kind');
      after = unhex(edit.after);
    }
    check(before.length === edit.length && after.length === edit.length, 'Patch length mismatch');
    for (let i = 0; i < edit.length; i++) {
      check(!touched.has(offset + i), 'Overlapping patch intervals'); touched.add(offset + i);
    }
    patched.set(after, offset);
  }
  const at = final.offset;
  const candidate = join([patched.subarray(0, at), header(4, low, linked.module.length, 0), linked.module, patched.subarray(at)]);
  candidate.set(header(0x5004, first.target, 0, candidate.length - 16));
  const result = readLdr(candidate);
  check(result.length === blocks.length + 1 && equal(candidate.subarray(-16), stock.subarray(-16)), 'Loader entry changed');
  const restored = join([candidate.subarray(0, at), candidate.subarray(at + 16 + linked.module.length)]);
  restored.set(stock.subarray(0, 16));
  for (const edit of p.patches) restored.set(unhex(edit.before), Number(edit.file_offset));
  check(equal(restored, stock), 'Unauthorized change outside reviewed OS edits');
  return candidate;
}

// Recover stock OS 1.2.1 from the stock file or from any open-xy OS made from
// it, including an earlier release's, whose module may sit elsewhere. The stock
// image is rebuilt by undoing every reviewed edit and accepted only by its
// SHA-256; the uploaded module is never executed or reused for a build. An exact
// output of this curated collection also restores its machine selection.
const RECOVERABLE_MACHINES = 10;
// Recovery tries every extension choice only for small collections; larger ones
// try the extensions whose code the module holds.
const RECOVERABLE_EXTENSIONS = 4;

// Whether an uploaded module appears to hold the given objects' code: their
// largest code section, byte for byte outside its relocated fields. Only a
// guess; recoverFirmware accepts it only by rebuilding it exactly.
function holdsCode(objects, payload) {
  let best = null;
  for (const o of objects) for (const s of o.sections)
    if (s.group === 'text' && (!best || s.size > best.s.size)) best = {o, s};
  if (!best || best.s.size < 16) return false;
  const {o, s} = best, fixed = new Uint8Array(s.size).fill(1);
  for (const r of o.relocations) if (r.section === s.index) {
    fixed.fill(0, [10, 13].includes(r.type) ? r.offset - 2 : r.offset, r.offset + (r.type === 18 ? 4 : 2));
  }
  const anchor = fixed.indexOf(1);
  if (anchor < 0) return false;
  for (let at = 0; at + s.size <= payload.length; at += 2) {
    if (payload[at + anchor] !== s.data[anchor]) continue;
    let i = anchor;
    while (i < s.size && (!fixed[i] || payload[at + i] === s.data[i])) i++;
    if (i === s.size) return true;
  }
  return false;
}
const presentMachines = (kit, payload) =>
  kit.machines.filter(m => holdsCode(kit.objects.filter(o => o.owner === m.id), payload)).map(m => m.id);
const presentExtensions = (kit, payload) => (kit.extensions ?? [])
  .filter(e => holdsCode(kit.objects.filter(o => o.extension === e.id), payload)).map(e => e.id);
const UPDATE = 'Update your OP–XY to OS 1.2.1 with teenage engineering’s updater, then copy its OS file (os.ldr) again; a .tfw update file will not work.';
const UNSUPPORTED = 'Only OP–XY OS 1.2.1 is supported. This file is not OS 1.2.1 or an open-xy OS made from it. ' + UPDATE;
export async function recoverFirmware(kit, input, layout = null) {
  const p = kit.profile;
  if (input.length === p.stock_bytes && await sha256(input) === p.stock_sha256) {
    readLdr(input);
    return {stock: input.slice(), selected: null};
  }
  const blocks = readLdr(input), first = blocks[0], module = blocks.at(-2), final = blocks.at(-1);
  check(blocks.length === p.stock_records + 1 && first.count === 0 &&
    (first.code & 0xffff) === 0x5004 && first.argument === input.length - 16 &&
    final.count === 0 && (final.code & 0xffff) === 0x8004 &&
    module.offset === p.stock_bytes - 16 && (module.code & 0xffff) === 4 &&
    module.argument === 0 && module.count > 0 && input.length === p.stock_bytes + 16 + module.count, UNSUPPORTED);
  const stock = join([input.subarray(0, module.offset), input.subarray(final.offset)]);
  stock.set(header(0x5004, first.target, 0, stock.length - 16));
  // Every released profile's edits are among these (tests/test_profile_parity.py).
  for (const edit of p.patches) stock.set(unhex(edit.before), Number(edit.file_offset));
  check(await sha256(stock) === p.stock_sha256,
    'Only OP–XY OS 1.2.1 is supported. This open-xy OS was not made from OS 1.2.1. ' + UPDATE);
  const payload = input.subarray(module.offset + 16, final.offset);
  if (module.target === p.module_base && module.count <= moduleBytes(p)) {
    // Reconstruct a selection's entire image; never trust the uploaded module
    // or infer ownership from its label. SDK builds retain timing; site builds
    // omit the per-voice bookkeeping. Accept only a complete reconstruction of
    // one mode, never mixed hooks.
    const rebuilds = async (selected, extensions) => {
      const linked = linkKit(kit, selected, layout, extensions);
      if (!equal(linked.module, payload)) return false;
      for (const profiling of [true, false]) {
        if (equal(await patchFirmware(kit, linked, stock, {profiling}), input)) return true;
      }
      return false;
    };
    // First the machines and extensions whose code the module holds, one link at
    // any kit size. Then every extension choice for a small collection, or the
    // present ones and none for a larger one, so links never grow exponentially.
    const present = presentMachines(kit, payload), held = presentExtensions(kit, payload);
    const guess = sharedClaim((kit.extensions ?? []).filter(e => held.includes(e.id))) ? [] : held;
    const choices = (kit.extensions ?? []).length <= RECOVERABLE_EXTENSIONS ? extensionChoices(kit)
      : guess.length ? [guess, []] : [[]];
    for (const extensions of present.length ? [guess, ...choices] : []) {
      try {
        if (await rebuilds(present, extensions)) return {stock, selected: present, extensions};
      } catch {}  // a guess that cannot link is not this kit's build
    }
    // Small kits also try every selection: at most 1,023 deterministic links per
    // extension choice, each under a millisecond. Yield between candidates so file
    // changes and navigation remain responsive; the caller discards stale results.
    if (kit.machines.length <= RECOVERABLE_MACHINES) {
      for (let mask = 1; mask < 2 ** kit.machines.length; mask++) {
        const selected = kit.machines.filter((_, i) => mask & 2 ** i).map(m => m.id);
        for (const extensions of choices) {
          if (await rebuilds(selected, extensions)) return {stock, selected, extensions};
        }
        await new Promise(resolve => setTimeout(resolve, 0));
      }
    }
  }
  // Another open-xy OS: an earlier release, other machines or another build.
  // Its stock OS is verified above; its machines and extensions are chosen again.
  return {stock, selected: null, earlier: true};
}

export function presetPath(machine) {
  if (machine.kind === 'effect') return null;
  check(['synth', 'sample'].includes(machine.kind) && /^[a-z][a-z0-9_]{0,14}$/.test(machine.id),
    'Invalid preset instrument');
  return `content/presets/${machine.id}/default.json`;
}

// The machine's init preset, then its own presets (tooling.machines.read_presets):
// each a version 4 preset of this machine, pointing at no samples. Its own
// default replaces the generated init preset.
export function presetFiles(machine) {
  const path = presetPath(machine);
  if (!path) return {};
  const presets = machine.presets ?? {};
  const files = Object.hasOwn(presets, 'default') ? {} : {[path]: defaultPreset(machine)};
  check(presets && typeof presets === 'object' && !Array.isArray(presets) && Object.keys(presets).length <= 16,
    'Invalid machine presets');
  for (const [name, preset] of Object.entries(presets)) {
    const params = preset?.engine?.params;
    check(/^[a-z0-9][a-z0-9_-]{0,23}$/.test(name) && preset && typeof preset === 'object' &&
      preset.version === 4 && preset.platform === 'OP-XY' && preset.type === machine.id &&
      Array.isArray(preset.regions) && preset.regions.length === 0 && Array.isArray(params) && params.length === 8 &&
      params.every(n => Number.isInteger(n) && n >= 0 && n <= 32767), 'Invalid machine preset: ' + name);
    files[`content/presets/${machine.id}/${name}.json`] = preset;
  }
  return files;
}

function defaultPreset(machine) {
  const entry = JSON.parse(machine.catalog.trim().replace(/^,/, ''));
  check(/^[a-z][a-z0-9_]{0,14}$/.test(entry.type) && Array.isArray(entry.default_params) &&
    entry.default_params.length === 8 && entry.default_params.every(n => Number.isInteger(n) && n >= 0 && n <= 32767),
    'Invalid preset engine or defaults');
  const defaults = machine.presetDefaults ?? {};
  check(defaults && typeof defaults === 'object' && !Array.isArray(defaults) &&
    Object.keys(defaults).every(key => key === 'amp'), 'Invalid preset defaults');
  const amp = defaults.amp ?? {};
  check(amp && typeof amp === 'object' && !Array.isArray(amp) &&
    Object.entries(amp).every(([key, value]) => ['attack', 'decay', 'release', 'sustain'].includes(key) &&
      Number.isInteger(value) && value >= 0 && value <= 32767), 'Invalid preset amp defaults');
  // Authored init settings in the device's version-4 preset structure. Engine
  // controls come from its catalog, never from a copied factory sound. Volume
  // 16384 is half scale: the device's gain readout shows 50.
  return {
    engine: {
      bendrange: 8191, highpass: 0,
      modulation: Object.fromEntries(['aftertouch', 'modwheel', 'pitchbend', 'velocity']
        .map(source => [source, {amount: 16384, target: 0}])),
      params: [...entry.default_params], playmode: 'poly',
      'portamento.amount': 0, 'portamento.type': 0, transpose: 0,
      'tuning.root': 0, 'tuning.scale': 0, 'velocity.sensitivity': 0, volume: 16384, width: 0,
    },
    envelope: {
      // Match a device-saved minimum amp release: the stored endpoint is 32767.
      amp: {attack: 128, decay: 8192, release: 32767, sustain: 32767, ...amp},
      filter: {attack: 0, decay: 0, release: 0, sustain: 32767},
    },
    fx: {active: false, params: [0, 0, 0, 0, 0, 32767, 0, 0], type: 'svf'},
    lfo: {active: false, params: [0, 0, 0, 0, 0, 0, 0, 0], type: 'value'},
    octave: 0, platform: 'OP-XY', regions: [], type: entry.type, version: 4,
  };
}

export async function buildAssets(kit, selection, catalog, numbers) {
  check(await sha256(catalog) === kit.profile.stock_catalog_sha256, 'The site’s machine data is damaged. Reload this page.');
  const machines = kit.machines.filter(m => selection.includes(m.id));
  // Each machine's entry ends its stock list; the offsets are bound to the
  // authenticated stock catalog (tooling.native.extend_catalog).
  const groups = {engine_types: machines.filter(m => m.kind !== 'effect'), fx_types: machines.filter(m => m.kind === 'effect')};
  const inserts = Object.entries(kit.profile.stock_catalog_inserts).sort((a, b) => b[1] - a[1]);
  check(inserts.map(([key]) => key).sort().join() === Object.keys(groups).sort().join(), 'Unexpected catalog insertion points');
  let extended = catalog;
  for (const [key, at] of inserts)
    extended = join([extended.subarray(0, at), utf8.encode(groups[key].map(m => catalogEntry(m, numbers)).join('')), extended.subarray(at)]);
  const text = decoder.decode(extended);
  const base = JSON.parse(decoder.decode(catalog)), expected = {...base};
  for (const [key, group] of Object.entries(groups))
    expected[key] = [...base[key], ...group.map(m => JSON.parse(catalogEntry(m, numbers).trim().replace(/^,/, '')))];
  check(JSON.stringify(JSON.parse(text)) === JSON.stringify(expected), 'Unexpected catalog modification');
  const files = {'content/op1_factory.json': extended};
  for (const machine of machines)
    for (const [path, preset] of Object.entries(presetFiles(machine)))
      files[path] = utf8.encode(JSON.stringify(preset, null, 2) + '\n');
  if (machines.some(m => m.artwork)) {
    check(await screenTemplateOk(kit), 'Platform screen template differs from the supported ABI');
    const baseSvg = kit.screenTemplate;
    check(baseSvg.split('</svg>').length === 2, 'Unexpected stock artwork boundary');
    for (const m of machines.filter(m => m.artwork)) {
      check(/^[a-z][a-z0-9_]*\.svg$/.test(m.asset), 'Invalid artwork filename');
      files['content/display/' + m.asset] = utf8.encode(baseSvg.replace('</svg>', m.artwork + '</svg>'));
    }
  }
  return files;
}

// Stored ZIP: no dependencies or compression worker, deterministic metadata.
export function zipFiles(files) {
  const locals = [], central = []; let offset = 0;
  const crc32 = bytes => {
    let c = 0xffffffff;
    for (const b of bytes) { c ^= b; for (let i = 0; i < 8; i++) c = c >>> 1 ^ (c & 1 ? 0xedb88320 : 0); }
    return (c ^ 0xffffffff) >>> 0;
  };
  for (const [name, bytes] of Object.entries(files)) {
    check(/^[a-zA-Z0-9_./-]+$/.test(name) && !name.includes('..') && !name.startsWith('/'), 'Invalid bundle path');
    const path = utf8.encode(name), crc = crc32(bytes), local = new Uint8Array(30 + path.length), d = view(local);
    d.setUint32(0, 0x04034b50, true); d.setUint16(4, 20, true); d.setUint16(12, 33, true);
    d.setUint32(14, crc, true); d.setUint32(18, bytes.length, true); d.setUint32(22, bytes.length, true);
    d.setUint16(26, path.length, true); local.set(path, 30);
    const entry = new Uint8Array(46 + path.length), e = view(entry);
    e.setUint32(0, 0x02014b50, true); e.setUint16(4, 20, true); e.setUint16(6, 20, true); e.setUint16(14, 33, true);
    e.setUint32(16, crc, true); e.setUint32(20, bytes.length, true); e.setUint32(24, bytes.length, true);
    e.setUint16(28, path.length, true); e.setUint32(42, offset, true); entry.set(path, 46);
    locals.push(local, bytes); central.push(entry); offset += local.length + bytes.length;
  }
  const directory = join(central), end = new Uint8Array(22), e = view(end);
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, central.length, true); e.setUint16(10, central.length, true);
  e.setUint32(12, directory.length, true); e.setUint32(16, offset, true);
  return join([...locals, directory, end]);
}
