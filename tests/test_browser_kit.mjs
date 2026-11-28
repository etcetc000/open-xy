import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {KIT_KEYS, kitChoices, kitGroup, kitHeader, kitKey, kitMix, kitModel, kitSelectedKey, kitWord} from '../builder/core.mjs';
import {RECORD_TABLE_BYTES, RECORDS_OBJECT, recordOrder, recordsObject, MACHINE_KEYWORDS, keywordsByUse, browseMachines, machineAuthor, CPU_HIGH_PERCENT, cpuWorstCase, cpuHigh, PRESET_NAME_LIMIT, readPresets, savePreset, deletePreset, presetMatches, applyPreset, activeServices, assignNumbers, checkedSelections, chosenByDefault, claimLabel, extensionChoices, extensionSelection, sharedClaim, numberLayout, readLayout, readSelection, readObject, linkKit, moduleBytes, expandedProfile, memoryKit, offersExpandedMemory, openKit, patchFirmware, presentSelections, readLdr, recoverFirmware, imageSettings, unitSettings, RECOVERABLE_ALTERNATIVES, sourceName, sourceUrl, validOrigin, validSource, zipFiles, sha256, readMachinePack, importMachinePack, hookSymbol} from '../builder/core.mjs';

// Tiny authored ELF fixture, independent of firmware and toolchains: one global, `symbol`,
// at the start of four code bytes.
function elf(symbol = 'entry') {
  const strings = Buffer.from('\0' + symbol + '\0'), names = Buffer.from('\0.text\0.symtab\0.strtab\0.shstrtab\0');
  // The names' last byte may share the null section header's first, which is 0 too.
  const at = 89 + strings.length, table = Math.max(128, (at + names.length - 1 + 3) & ~3);
  const bytes = new Uint8Array(table + 5 * 40), d = new DataView(bytes.buffer);
  const w16 = (at, n) => d.setUint16(at, n, true), w32 = (at, n) => d.setUint32(at, n, true);
  bytes.set([127,69,76,70,1,1,1]); w16(16,1); w16(18,106); w32(20,1); w32(32,table);
  w16(40,52); w16(46,40); w16(48,5); w16(50,4);
  bytes.set([0,96,16,0],52); // R0=0; RTS.
  w32(72,1); w32(80,4); bytes[84] = 0x12; w16(86,1);
  bytes.set(strings,88); bytes.set(names,at);
  const section = (i, fields) => fields.forEach((v,j) => w32(table + i * 40 + j * 4,v));
  section(1,[1,1,6,0,52,4,0,0,4,0]);
  section(2,[7,2,0,0,56,32,3,1,4,16]);
  section(3,[15,3,0,0,88,strings.length,0,0,1,0]);
  section(4,[23,3,0,0,at,names.length,0,0,1,0]);
  return bytes;
}
function machine(id, engine, page, origin='personal') { return {id, prefix:id, kind:'synth', identity:{engine,page}, origin}; }
const FOUR_MIB = 4 * 1024 ** 2;
const profileBytes = readFileSync(new URL('../profiles/opxy-1.2.1.json',import.meta.url));
function fixture() {
  const object = {...readObject(elf()), name:'a.o',owner:'a'};
  return {version:'0.0.2',sdk:'c'.repeat(64),profile:JSON.parse(profileBytes),definitions:{},fixed:{a:{engine:34,page:82}},machines:[machine('a',34,82)],objects:[object]};
}

const SDK = 'c'.repeat(64);
async function capsuleOf(name, owner, symbol) {
  const bytes = elf(symbol);
  return {name, owner, bytes:Buffer.from(bytes).toString('base64'), sha256:await sha256(bytes)};
}
// A site kit as exported and a pack of two custom machines made with the same SDK: each defines
// its record, as every machine's generated objects do (tooling/records.py).
async function packFixture() {
  const k = fixture();
  Object.assign(k, {format:'opxy-browser-kit/1', sdk:SDK, profileSource:profileBytes.toString('base64'), notices:'SDK',
    stockCatalog:'', screenTemplate:readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg', import.meta.url), 'utf8')});
  k.objects = [await capsuleOf('a/voice.o', 'a', 'entry'), await capsuleOf('sdk/memory.o', null, 'share'),
    await capsuleOf('sdk/startup.o', null, 'start')];
  // The site's kit as exported, which the importer opens itself.
  const base = k;
  const custom = id => ({...machine(id, null, null), name:id, description:'A custom machine',
    controls:['A', 'B', 'C', 'D'], positions:[0, 1, 2, 3], pages:false,
    catalog:', {"type":"' + id + '","default_params":[0,0,0,0,0,0,0,0]}',
    artwork:'<rect id="panel" width="1" height="1"/>', asset:id + '.svg'});
  const pack = {format:'opxy-machine-pack/2', version:k.version, sdk:SDK, profileSha256:await sha256(profileBytes),
    definitions:structuredClone(k.definitions), fixed:structuredClone(k.fixed), machines:[custom('custom'), custom('another')],
    objects:[await capsuleOf('custom/voice.o', 'custom', 'custom_record'),
      await capsuleOf('another/voice.o', 'another', 'another_record')], notices:'Author license'};
  return {base, pack};
}
// The site links the objects of these services (a kit's `service` objects); a pack never carries them.
async function withServices(base, services) {
  for (const [i, service] of services.entries())
    base.objects.push({...await capsuleOf('sdk/' + service + '.o', null, 'svc' + String(i).padStart(2, '0')), service});
}

test('machine ZIP adds several selectable machines without changing the base kit or saved numbers', async () => {
  const {base, pack} = await packFixture(), before = structuredClone(base);
  const zip = zipFiles({'machines.json':new TextEncoder().encode(JSON.stringify(pack))});
  const candidate = await importMachinePack(base, readMachinePack(zip));
  assert.deepEqual(base, before);
  assert.deepEqual(candidate.machines.map(m => m.id), ['a', 'custom', 'another']);
  assert.equal(candidate.profile, base.profile);
  assert.equal(candidate.stockCatalog, base.stockCatalog);
  assert.match(candidate.notices, /Author license/);
  const layout = {a:{engine:34, page:82}, custom:{engine:55, page:105}, removed:{engine:56, page:106}};
  const linked = linkKit(candidate, ['a', 'custom', 'another'], layout);
  assert.deepEqual(linked.numbers.custom, layout.custom);
  assert.deepEqual(linked.numbers.removed, layout.removed);
  assert.ok(linked.globals.has('custom_record') && linked.globals.has('another_record'));
  assert.ok(!linkKit(candidate, ['a']).globals.has('custom_record'));
  // Its objects follow the site's machines' and come before the SDK's, as a native build links them.
  assert.deepEqual(candidate.objects.map(o => o.name),
    ['a/voice.o', 'custom/voice.o', 'another/voice.o', 'sdk/memory.o', 'sdk/startup.o']);
});

test('machine packs reject catalog replacement, runtime changes and malformed imported machines atomically', async () => {
  const {base, pack} = await packFixture(), before = structuredClone(base);
  const changes = [
    p => { p.version = '99.0.0'; }, p => { p.profileSha256 = '0'.repeat(64); }, p => { p.sdk = 'd'.repeat(64); },
    p => { delete p.sdk; }, p => { p.format = 'opxy-machine-pack/1'; },
    p => { p.definitions.changed = 123; }, p => { p.fixed.changed = {engine:40, page:90}; },
    p => { p.extensions = []; }, p => { p.stockCatalog = 'injected'; },
    p => { p.machines[0].id = 'a'; p.machines[0].catalog = ', {"type":"a","default_params":[0,0,0,0,0,0,0,0]}'; },
    p => { p.machines[0].prefix = 'a'; }, p => { p.machines[1] = structuredClone(p.machines[0]); },
    p => { p.machines[0].origin = 'official'; }, p => { p.machines[0].source = {repository:'https://evil.test'}; },
    p => { p.machines[0].asset = '../outside.svg'; }, p => { p.machines[0].artwork = '<script>bad()</script>'; },
    p => { p.machines[0].icon = '\"/><image href="https://evil.test"/>'; },
    p => { p.machines[0].catalog = ', {"type":"a","default_params":[]}'; },
    p => { p.machines[0].absent = {custom_ready:'kit_absent'}; },
    p => { p.machines = []; p.objects = []; },
    p => { p.objects[0].bytes = p.objects[1].bytes; },
    p => { p.objects[0].sha256 = '0'.repeat(64); }, p => { p.objects[1].owner = 'custom'; },
    p => { p.objects[0].name = 'sdk/injected.o'; }, p => { p.objects[0].owner = null; },
    p => { p.objects.splice(1, 1); }, p => { p.objects.push(p.objects[0]); },
    p => { p.objects[0].service = 'samples'; }, p => { p.objects[0].runtime = true; },
    p => { p.objects[0].extra = true; },
  ];
  for (const change of changes) {
    const changed = structuredClone(pack); change(changed);
    await assert.rejects(importMachinePack(base, changed), undefined, change.toString());
    assert.deepEqual(base, before);
  }
});

test('a pack imports into any catalog of its release and SDK, matched by ID', async () => {
  // The site's catalog gained a machine, lost one or lists them in another order since the pack's
  // author exported it: only the pack's own machines and objects enter.
  for (const change of [async k => {
    k.machines.push(machine('b', 35, 83));
    k.objects.splice(1, 0, await capsuleOf('b/voice.o', 'b', 'other'));
  }, async k => {
    k.machines.unshift(machine('b', 35, 83));
    k.objects.unshift(await capsuleOf('b/voice.o', 'b', 'other'));
  }]) {
    const {base, pack} = await packFixture();
    await change(base);
    const candidate = await importMachinePack(base, pack);
    assert.deepEqual(candidate.machines.map(m => m.id), [...base.machines.map(m => m.id), 'custom', 'another']);
    assert.ok(linkKit(candidate, ['custom', 'b']).globals.has('custom_record'));
  }
  // A pack made with another SDK revision is refused.
  const {base, pack} = await packFixture();
  base.sdk = 'd'.repeat(64);
  await assert.rejects(importMachinePack(base, pack), /different Open-XY release or SDK revision/);
  delete base.sdk;
  await assert.rejects(importMachinePack(base, pack), /different Open-XY release or SDK revision/);
});

test('a pack keeps the site kit unit sources and adds no SDK objects', async () => {
  const {base, pack} = await packFixture();
  // The site's unit capability objects, as exported: the importer opens the kit as it was sent.
  const unit = Buffer.from(base.objects[1].bytes, 'base64'); unit[52] ^= 1;
  base.unitSources = {fast_helpers:{0:[{name:'sdk/memory.o', bytes:base.objects[1].bytes, sha256:base.objects[1].sha256}],
    1:[{name:'sdk/memory.o', bytes:unit.toString('base64'), sha256:await sha256(unit)}]}};
  const candidate = await importMachinePack(base, pack);
  assert.equal(candidate.unitSources.fast_helpers['1'][0].path, 'unit-sources/fast_helpers/1/sdk/memory.o');
  // No SDK object, runtime member included, comes from a pack.
  const {base:site, pack:extra} = await packFixture();
  extra.objects.push({...await capsuleOf('sdk/runtime/_mulsi3.o', null, 'mulsi'), runtime:true});
  await assert.rejects(importMachinePack(site, extra), /unowned machine objects/);
});

test('a pack machine needs its record and may not define what SDK objects use', async () => {
  const {base, pack} = await packFixture();
  // The site's startup imports `share`, which its memory object defines.
  const startup = elf('share'); startup[84] = 0x10; startup[86] = 0;
  base.objects[2] = {name:'sdk/startup.o', owner:null, bytes:Buffer.from(startup).toString('base64'), sha256:await sha256(startup)};
  pack.objects.push(await capsuleOf('custom/share.o', 'custom', 'share'));
  await assert.rejects(importMachinePack(base, pack), /custom defines share, which the SDK's objects use/);
  const {base:site, pack:bare} = await packFixture();
  bare.objects[0] = await capsuleOf('custom/voice.o', 'custom', 'other');
  await assert.rejects(importMachinePack(site, bare), /custom has no machine record/);
  // A symbol a site machine defines too: a native build of both would refuse it.
  const {base:other, pack:clash} = await packFixture();
  clash.objects.push(await capsuleOf('custom/helper.o', 'custom', 'entry'));
  await assert.rejects(importMachinePack(other, clash), /custom defines entry, which a also defines/);
  const {base:third, pack:twice} = await packFixture();
  twice.objects.push(await capsuleOf('another/helper.o', 'another', 'custom_record'));
  await assert.rejects(importMachinePack(third, twice), /defines custom_record, which (custom|another) also defines/);
});

test('packs extend site kits with aux machines and whatever extensions they offer', async () => {
  const {base, pack} = await packFixture();
  // An aux machine has no menu entry; the pack and the site list the same machine.
  base.machines[0].catalog = '';
  await importMachinePack(base, pack);
  // Whether cpu is the SDK's own depends on the exporting checkout (browser_kit.sdk_extension).
  const extension = {id:'cpu', name:'CPU', description:'Settings', origin:'official', claims:[], actions:[],
    claim_labels:{}, capabilities:['boot_log_fix'], linked:false};
  base.extensions = [extension];
  assert.deepEqual((await importMachinePack(base, pack)).extensions.map(e => e.id), ['cpu']);
});

test('packs refuse menus without an effect number and IDs longer than layouts hold', async () => {
  const cases = [
    [p => { p.machines[0].kind = 'effect'; p.machines[0].identity = {effect:null, page:21}; }, /menu/],
    [p => { p.machines[0].catalog = ', {"type":"custom","id":3,"default_params":[0,0,0,0,0,0,0,0]}'; }, /menu/],
    // An effect number stock does not offer would stay in the menu (catalogEntry replaces digits only).
    ...[-5, 99, 1e21].map(id => [p => {
      Object.assign(p.machines[0], {kind:'effect', identity:{effect:null, page:21}, controls:['A'], positions:[0],
        catalog:`, {"type":"custom","id":${id},"default_params":[0,0,0,0,0,0,0,0]}`});
    }, /menu/]),
    [p => { for (const key of ['id', 'prefix']) p.machines[0][key] = 'custommachinelong'; }, /metadata/],
    // Not an object, or not JSON: the same refusal, not a parser error.
    ...[', null', ', [1]', ', "custom"', ', {"type":'].map(catalog => [p => { p.machines[0].catalog = catalog; }, /Invalid machine menu/]),
  ];
  for (const [change, error] of cases) {
    const {base, pack} = await packFixture(); change(pack);
    await assert.rejects(importMachinePack(base, pack), error, change.toString());
  }
});

test('an imported menu entry is rebuilt from its checked values in the exporter layout', async () => {
  const {base, pack} = await packFixture(), m = pack.machines[0];
  Object.assign(m, {kind:'effect', identity:{effect:null, page:21}, controls:['A'], positions:[0],
    // A repeated key: JSON keeps the last, and so does the rebuilt entry.
    catalog:', {"type":"custom","id":21,"default_params":[1,2,3,4,5,6,7,8],"id":23}'});
  const candidate = await importMachinePack(base, pack);
  const indent = '\n            ';
  assert.equal(candidate.machines[1].catalog, ',\n        {' + indent + '"type": "custom",' + indent + '"id": 23,' +
    indent + '"default_params": [1, 2, 3, 4, 5, 6, 7, 8]\n        }');
  assert.equal(candidate.machines[2].catalog, ',\n        {' + indent + '"type": "another",' + indent +
    '"default_params": [0, 0, 0, 0, 0, 0, 0, 0]\n        }\n');
});

test('packs take authored descriptions and artwork comments as the SDK does, and check grayscale artwork', async () => {
  const {base, pack} = await packFixture(), m = pack.machines[0];
  // 160 characters as machine.json counts them (code points), and a comment the exporter keeps.
  Object.assign(m, {description:'\u{1F3B9}'.repeat(160), artwork:'<!-- one = first --><rect id="panel" width="1" height="1"/>'});
  await importMachinePack(base, pack);
  m.description += '.';
  await assert.rejects(importMachinePack(base, pack), /metadata/);
  m.description = 'A custom machine';
  m.grayscaleArtwork = '<script>bad()</script>';
  await assert.rejects(importMachinePack(base, pack), /artwork/);
  delete m.grayscaleArtwork;
  // Markup hidden from a comment strip, a prefixed or unknown element, and names stock or the SDK use.
  for (const artwork of ['<text><![CDATA[<!--]]></text><script>bad()</script><text><![CDATA[-->]]></text>',
    '<?x <!--?><script>bad()</script><?x -->?>', '<svg:script xmlns:svg="http://www.w3.org/2000/svg">bad()</svg:script>',
    '<animate attributeName="x"/>', '<a><rect/></a>', '<rect style="fill:red"/>', '<!-- open <rect/>',
    '<!DOCTYPE svg><rect/>', '<rect fill="&#117;rl(#x)"/>', '<rect onclick="x()"/>', '<rect xlink:href="#x"/>',
    '<rect fill="&#x110000;"/>', '<rect fill="&#99999999;"/>']) {
    m.artwork = artwork;
    await assert.rejects(importMachinePack(base, pack), /artwork/, artwork);
  }
  // What the SDK's authored SVG may hold: a font-style, a backslash or url( in text, CDATA text,
  // and a comment holding markup.
  for (const artwork of ['<text font-style="italic" x="1">A\\B url(x)</text>', '<text><![CDATA[a < b]]></text>',
    '<!-- <!DOCTYPE x> <script> --><g><rect width="1" height="1"/></g>']) {
    m.artwork = artwork;
    await importMachinePack(base, pack);
  }
  // `zz` holds fixed numbers, as Open-XY's own machines do on every site, though this site lacks it.
  for (const id of ['reverb', 'sdk', 'kit_custom', 'zz']) {
    const {base, pack} = await packFixture();
    base.fixed.zz = pack.fixed.zz = {engine:40, page:90};
    Object.assign(pack.machines[0], {id, prefix:id, asset:id + '.svg',
      catalog:`, {"type":"${id}","default_params":[0,0,0,0,0,0,0,0]}`});
    await assert.rejects(importMachinePack(base, pack), /metadata/, id);
  }
});

test('packs refuse kinds they cannot import and malformed services or heap voices', async () => {
  for (const [change, error] of [[m => { m.kind = 'drum'; }, /metadata/], [m => { m.kind = 'player'; }, /metadata/],
    [m => { m.heapVoices = -1; }, /heap voices/], [m => { m.heapVoices = 1.5; }, /heap voices/],
    [m => { m.shiftRow = 'yes'; }, /Shift row/],
    [m => { m.unknown = true; }, /metadata/], [m => { m.keywords = ['bass']; }, /metadata/],
    [m => { m.auxPresets = {'../../x.json':'{}'}; }, /metadata/], [m => { m.fullCanvas = 'x'; }, /full canvas/],
    [m => { m.preview = {wasm:'', sha256:'0'.repeat(64), defaults:[0, 0, 0, 0], kit:{models:1e9}}; }, /preview/],
    [m => { m.preview = {wasm:'', sha256:'0'.repeat(64), defaults:[0, 0, 0, 0], shift:Array(8).fill(0)}; }, /preview/],
    // Only the features an imported kind declares: never a capability, another kind or an aux track.
    ...['Bad', 'control', 'load_governor', 'release_floor', 'extension', 'drum', 'padMix', 'auxTrack', 'kitControls']
      .map(f => [m => { m.features = [f]; }, /features/]),
    [m => { m.features = 'hidePages'; }, /features/],
    [m => { m.kind = 'sample'; m.identity = {engine:null, page:7}; m.features = ['synthShift']; }, /features/]]) {
    const {base, pack} = await packFixture(); change(pack.machines[0]);
    await assert.rejects(importMachinePack(base, pack), error, change.toString());
  }
  const {base, pack} = await packFixture();
  Object.assign(pack.machines[0], {features:['hidePages', 'synthShift'], shiftRow:true, heapVoices:4096});
  // Each service these features switch on with hooks must be the site's.
  await assert.rejects(importMachinePack(base, pack), /needs the control service/);
  await withServices(base, ['control', 'drums', 'drum_locks', 'synth_shift']);
  await importMachinePack(base, pack);
  pack.machines[0].shiftRow = undefined;
  await assert.rejects(importMachinePack(base, pack), /Shift row/);
});

test('packs accept the SDK control counts, absent synth slots and source filenames', async () => {
  for (const kind of ['effect', 'sample']) for (const count of [1, 2, 3]) {
    const {base, pack} = await packFixture(), m = pack.machines[0];
    if (kind === 'sample') await withServices(base, ['samples', 'octaves']);
    Object.assign(m, {kind, identity:kind === 'effect' ? {effect:null, page:21} : {engine:null, page:7},
      controls:m.controls.slice(0, count), positions:m.positions.slice(0, count),
      // An effect's menu entry carries the number its author's collection gave it.
      ...(kind === 'effect' ? {catalog:', {"type":"custom","id":21,"default_params":[0,0,0,0,0,0,0,0]}'} : {})});
    await importMachinePack(base, pack);
    m.positions[0] = null;
    await assert.rejects(importMachinePack(base, pack), /controls/);
  }
  const {base, pack} = await packFixture();
  pack.machines[0].positions[1] = null; pack.machines[0].controls[1] = '';
  for (const name of ['Wave.o', 'wave.v2.o', 'wave..v3.o']) {
    pack.objects[0].name = 'custom/' + name;
    await importMachinePack(base, pack);
  }
  for (const name of ['../Wave.o', 'lib/../../Wave.o', '/Wave.o', 'C:/Wave.o']) {
    pack.objects[0].name = name;
    await assert.rejects(importMachinePack(base, pack), /unowned machine objects/);
  }
  pack.objects[0].name = 'custom/../Wave.o';
  await assert.rejects(importMachinePack(base, pack), /unowned machine objects/);
});

test('a pack machine the site already has is refused, whatever its metadata', async () => {
  const {base, pack} = await packFixture();
  base.machines.push({...pack.machines[0], origin:'catalog'});
  await assert.rejects(importMachinePack(base, pack), /custom is already one of this site's machines/);
});

test('machine ZIP reader rejects corruption, archive ambiguities and source/firmware archives', () => {
  const encode = value => new TextEncoder().encode(value);
  const good = zipFiles({'machines.json':encode('{"format":"test"}')});
  assert.equal(readMachinePack(good).format, 'test');
  for (let length = 0; length < good.length; length++) assert.throws(() => readMachinePack(good.slice(0, length)));
  for (const at of [6, 8, 14, 18, 22, 26, 28, 30, 44, good.length - 22, good.length - 14, good.length - 2]) {
    const corrupt = good.slice(); corrupt[at] ^= 1;
    assert.throws(() => readMachinePack(corrupt), undefined, 'offset ' + at);
  }
  for (const files of [{'machine.json':encode('{}')}, {'os.ldr':encode('not firmware')},
    {'machines.json':encode('{}'), 'extra.txt':encode('x')},
    {'machines.json':encode('{"__proto__":{"x":true}}')}]) assert.throws(() => readMachinePack(zipFiles(files)));
  assert.throws(() => readMachinePack(new Uint8Array([...good, 0])));
});

// An authored loader with two diagnostic hooks and one ordinary hook. No stock
// instructions or firmware bytes are needed to exercise full reconstruction.
async function profilingFixture(extra = 0) {
  const kit = fixture(), stock = new Uint8Array(96 + extra), d = new DataView(stock.buffer);
  const record = (at, flags, address, size, argument = 0) => {
    [0xad000000 | flags, address, size, argument].forEach((v, i) => d.setUint32(at + i * 4, v, true));
    stock[at + 2] = stock.subarray(at, at + 16).reduce((a, b) => a ^ b, 0);
  };
  record(0, 0x5004, 0x1000, 0, stock.length - 16);
  record(16, 4, 0x1000, 32 + extra);
  stock.set(Array.from({length:32 + extra}, (_, i) => i + 1), 32);
  record(64 + extra, 0x104, 0x2000, 256);
  record(80 + extra, 0x8004, 0, 0);
  Object.assign(kit.profile, {stock_bytes:stock.length, stock_sha256:await sha256(stock), stock_records:4,
    module_base:0x2000, reserved_bytes:256, table_bytes:0, heap_fill_base:0x2000, heap_fill_bytes:256,
    patches:[0, 8, 16].map((offset, i) => ({kind:'hook', call:true, symbol:'entry', length:8,
      address:0x1000 + offset, file_offset:32 + offset,
      before:Buffer.from(stock.subarray(32 + offset, 40 + offset)).toString('hex'),
      ...(i < 2 ? {profiling:false} : {})}))});
  return {kit, stock, linked:linkKit(kit, ['a'])};
}

test('profiling defaults on; disabling it changes only the reviewed diagnostic hooks', async () => {
  const {kit, linked, stock} = await profilingFixture(), original = stock.slice();
  const on = await patchFirmware(kit, linked, stock);
  assert.deepEqual(on, await patchFirmware(kit, linked, stock, {profiling:true}));
  const off = await patchFirmware(kit, linked, stock, {profiling:false});
  assert.deepEqual(stock, original);
  for (const edit of kit.profile.patches) {
    const at = edit.file_offset;
    if (edit.profiling === false) {
      assert.deepEqual(on.subarray(at, at + 8), stock.subarray(at, at + 8));
      assert.notDeepEqual(off.subarray(at, at + 8), on.subarray(at, at + 8));
    } else {
      assert.deepEqual(off.subarray(at, at + 8), on.subarray(at, at + 8));
      assert.notDeepEqual(on.subarray(at, at + 8), stock.subarray(at, at + 8));
    }
  }
  const restored = off.slice(); restored.set(on.subarray(32, 48), 32);
  assert.deepEqual(restored, on);
  for (const image of [on, off]) assert.deepEqual(await recoverFirmware(kit, image), {stock, selected:['a'], extensions:[], memory:'standard'});
});

test('a settings extension links no code, applies only its capability edits and is recovered from them', async () => {
  const {kit, stock} = await profilingFixture();
  const before = Buffer.from(stock.subarray(56, 64)).toString('hex');
  kit.profile.services = {...kit.profile.services, long_samples:['long_samples']};
  kit.profile.patches.push({kind:'limit', service:'long_samples', address:0x1018, file_offset:56, length:8,
    before, after:'01da0f00ffff0000'});
  kit.extensions = [{id:'longer', claims:[], actions:[], capabilities:['long_samples'], linked:false}];
  const on = linkKit(kit, ['a'], null, ['longer']), off = linkKit(kit, ['a'], null, []);
  assert.deepEqual([...on.services], ['long_samples']);  // no extension runtime
  assert.deepEqual(on.module, off.module);
  const longer = await patchFirmware(kit, on, stock), plain = await patchFirmware(kit, off, stock);
  assert.equal(Buffer.from(longer.subarray(56, 64)).toString('hex'), '01da0f00ffff0000');
  assert.equal(Buffer.from(plain.subarray(56, 64)).toString('hex'), before);
  longer.subarray(56, 64).forEach((v, i) => { if (v !== plain[56 + i]) plain[56 + i] = v; });
  assert.deepEqual(plain, longer);  // the capability's edit is the only difference
  assert.deepEqual(await recoverFirmware(kit, longer), {stock, selected:['a'], extensions:['longer'], memory:'standard'});
  assert.deepEqual(await recoverFirmware(kit, await patchFirmware(kit, off, stock)), {stock, selected:['a'], extensions:[], memory:'standard'});
});

// A helper object as a unit capability's sources make them (tooling.native UNIT_SOURCES): the fixture
// ELF defining `helpr` with its four code bytes.
function helper(code) { const bytes = elf(); bytes.set(Buffer.from('helpr'), 89); bytes.set(code, 52); return bytes; }
const capsule = (name, bytes, extra = {}) => ({name, owner:null, ...extra, bytes:Buffer.from(bytes).toString('base64')});

test('a kit carries a capability only with what the browser needs for it', async () => {
  const k = fixture();
  Object.assign(k, {format:'opxy-browser-kit/1', profileSource:profileBytes.toString('base64'), notices:'SDK',
    stockCatalog:'', screenTemplate:readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg', import.meta.url), 'utf8')});
  const objects = [capsule('a.o', elf(), {owner:'a'})];
  for (const o of objects) o.sha256 = await sha256(Buffer.from(o.bytes, 'base64'));
  k.objects = objects;
  const meta = {name:'Settings', description:'Turns on a capability.', origin:'official', claim_labels:{}, claims:[], actions:[], linked:false};
  const opens = async (capability, overrides = {}) => {
    Object.assign(k, {objects, unitSources:undefined}, overrides);
    k.extensions = [{...meta, id:'faster', capabilities:[capability]}];
    return openKit(k);
  };
  // Image edits: long_samples, boot_log_fix, and load_governor's and pitch_constants' fixed bytes,
  // which the profile marks as the build's: the browser writes them.
  for (const capability of ['long_samples', 'boot_log_fix', 'load_governor', 'pitch_constants']) await opens(capability);
  // Hooks into SDK code: only with that service's own objects in the kit, which the export compiles
  // (block_trims, output_slack). idle changes every machine's generated code, so a kit never holds
  // its objects (the export refuses it: tooling.browser_kit.refuse_adapter_capabilities);
  // master_filter is no registry capability, so no kit object implements it as one.
  for (const capability of ['block_trims', 'output_slack', 'idle', 'master_filter'])
    await assert.rejects(opens(capability), /Invalid extension capabilities/, capability);
  const hooks = capsule('sdk/track_hooks.o', helper([0, 96, 16, 0]), {service:'block_trims'});
  hooks.sha256 = await sha256(Buffer.from(hooks.bytes, 'base64'));
  await opens('block_trims', {objects:[...objects, hooks]});
  await assert.rejects(opens('output_slack', {objects:[...objects, hooks]}), /Invalid extension capabilities/);
  // fast_helpers has no edit: only with its units' objects for each value (kit.unitSources).
  await assert.rejects(opens('fast_helpers'), /Invalid extension capabilities/);
  const plain = capsule('sdk/divide.o', helper([0, 96, 16, 0])), fast = capsule('sdk/divide.o', helper([1, 96, 16, 0]));
  for (const o of [plain, fast]) o.sha256 = await sha256(Buffer.from(o.bytes, 'base64'));
  const withDivide = [...objects, plain];
  const units = {fast_helpers:{'0':[{name:plain.name, bytes:plain.bytes, sha256:plain.sha256}],
    '1':[{name:fast.name, bytes:fast.bytes, sha256:fast.sha256}]}};
  const opened = await opens('fast_helpers', {objects:withDivide, unitSources:units});
  assert.equal(opened.unitSources.fast_helpers['1'][0].path, 'unit-sources/fast_helpers/1/sdk/divide.o');
  // Its setting: each value of its range has the kit's objects.
  const setting = {label:'Fast helpers', unit:'0 off, 1 on', minimum:0, maximum:1, default:0, value:0};
  k.extensions = [{...meta, id:'faster', capabilities:['fast_helpers'], settings:{fast_helpers:setting}}];
  await openKit({...k, objects:withDivide, unitSources:units});
  await assert.rejects(openKit({...k, objects:withDivide, unitSources:units,
    extensions:[{...k.extensions[0], settings:{fast_helpers:{...setting, maximum:2}}}]}), /Invalid extension settings/);
  for (const [why, unitSources, kitObjects] of [
    ['a value missing', {fast_helpers:{'0':units.fast_helpers['0']}}, withDivide],
    ['an object the kit lacks', units, objects],
    ['a capability with edits', {...units, long_samples:units.fast_helpers}, withDivide],
    ['another digest', {fast_helpers:{...units.fast_helpers, '1':[{...units.fast_helpers['1'][0], sha256:plain.sha256}]}}, withDivide],
    ['values naming other objects', {fast_helpers:{...units.fast_helpers, '1':[{...units.fast_helpers['1'][0], name:'a.o'}]}}, withDivide]])
    await assert.rejects(opens('fast_helpers', {objects:kitObjects, unitSources}), /unit sources|hash mismatch/, why);
});

test('a unit capability links its chosen value\'s objects in place of the build\'s, and is recovered with it', async () => {
  const {kit, stock} = await profilingFixture();
  const plain = helper([0, 96, 16, 0]), fast = helper([1, 96, 16, 0]);
  kit.objects.push({...readObject(plain), name:'sdk/divide.o', owner:null});
  kit.profile.services = {...kit.profile.services, fast_helpers:['fast_helpers']};
  const variant = (value, bytes) => [{...readObject(bytes), name:'sdk/divide.o', owner:null, path:`unit-sources/fast_helpers/${value}/sdk/divide.o`}];
  kit.unitSources = {fast_helpers:{'0':variant(0, plain), '1':variant(1, fast)}};
  const setting = {label:'Fast helpers', unit:'0 off, 1 on', minimum:0, maximum:1, default:0};
  kit.extensions = [{id:'faster', claims:[], actions:[], capabilities:['fast_helpers'], linked:false, settings:{fast_helpers:{...setting, value:0}}}];
  const without = linkKit(kit, ['a']), own = linkKit(kit, ['a'], null, ['faster']);
  const on = linkKit(kit, ['a'], null, ['faster'], {fast_helpers:1});
  // Without the extension a chosen value is ignored: every build without it links the plain sources.
  assert.deepEqual(linkKit(kit, ['a'], null, [], {fast_helpers:1}).module, without.module);
  assert.deepEqual(own.module, without.module);
  assert.deepEqual([without.units, own.units, on.units], [{fast_helpers:0}, {fast_helpers:0}, {fast_helpers:1}]);
  assert.deepEqual(unitSettings(kit, ['faster'], {fast_helpers:1}), {fast_helpers:1});
  // The same names in the same place: only the helper's bytes differ.
  assert.deepEqual(on.objects.map(o => o.name), without.objects.map(o => o.name));
  assert.equal(on.objects[1].path, 'unit-sources/fast_helpers/1/sdk/divide.o');
  assert.deepEqual([...on.module].map((b, i) => b === without.module[i] ? 0 : i).filter(Boolean), [4]);
  assert.deepEqual(on.globals, without.globals);
  // The extension's own value is the default; a value outside the kit's is refused.
  kit.extensions[0].settings.fast_helpers.value = 1;
  assert.deepEqual(linkKit(kit, ['a'], null, ['faster']).module, on.module);
  assert.throws(() => linkKit(kit, ['a'], null, ['faster'], {fast_helpers:2}), /outside its range/);
  kit.extensions[0].settings.fast_helpers.value = 0;
  // The OS is patched for the values the module was linked with.
  await assert.rejects(patchFirmware(kit, on, stock), /linked for other capability settings/);
  const image = await patchFirmware(kit, on, stock, {settings:{fast_helpers:1}});
  // No image site shows the value: recovery tries each and reports the one that rebuilds it.
  assert.deepEqual(imageSettings(kit, image), {});
  assert.deepEqual(await recoverFirmware(kit, image), {stock, selected:['a'], extensions:['faster'], memory:'standard', settings:{fast_helpers:1}});
  // At 0 the image is the one without the extension, which recovery tries first.
  assert.deepEqual(await recoverFirmware(kit, await patchFirmware(kit, own, stock)), {stock, selected:['a'], extensions:[], memory:'standard'});
});

test('a capability setting writes the reviewed bytes of its value and is recovered with it', async () => {
  // As release_floor: a setting edit whose minimum is stock's bytes, and a fixed edit
  // (stage 0 -> 5) that marks the capability on.
  const {kit, stock} = await profilingFixture(8);
  const before = Buffer.from(stock.subarray(56, 64)).toString('hex'), stage = Buffer.from(stock.subarray(64, 66)).toString('hex');
  const choices = {'-40':'01da470114ae0000', '-48':'01da8200a6730000', '-66':before};
  kit.profile.services = {...kit.profile.services, release_floor:['release_floor']};
  kit.profile.patches.push({kind:'limit', service:'release_floor', setting:'release_floor', address:0x1018, file_offset:56,
    length:8, before, after:choices['-48'], choices},
  {kind:'limit', service:'release_floor', address:0x1020, file_offset:64, length:2, before:stage, after:'2860'});
  const setting = {label:'Idle threshold', unit:'dB', minimum:-66, maximum:-20, default:-48};
  kit.extensions = [{id:'idle', claims:[], actions:[], capabilities:['release_floor'], linked:false,
    settings:{release_floor:{...setting, value:-40}}}];
  const on = linkKit(kit, ['a'], null, ['idle']), off = linkKit(kit, ['a'], null, []);
  const site = image => Buffer.from(image.subarray(56, 64)).toString('hex');
  // The extension's own value, then a chosen one; without the extension, stock.
  assert.equal(site(await patchFirmware(kit, on, stock)), choices['-40']);
  const chosen = await patchFirmware(kit, on, stock, {settings:{release_floor:-66}});
  assert.equal(site(chosen), before);
  assert.equal(site(await patchFirmware(kit, off, stock)), before);
  await assert.rejects(patchFirmware(kit, on, stock, {settings:{release_floor:-30}}), /outside its range/);
  const built = await patchFirmware(kit, on, stock);
  assert.deepEqual(imageSettings(kit, built), {release_floor:-40});
  assert.deepEqual(await recoverFirmware(kit, built),
    {stock, selected:['a'], extensions:['idle'], memory:'standard', settings:{release_floor:-40}});
  // The minimum's bytes equal stock's, but the fixed edit says the capability is on.
  assert.deepEqual(imageSettings(kit, chosen), {release_floor:-66});
  assert.deepEqual(await recoverFirmware(kit, chosen),
    {stock, selected:['a'], extensions:['idle'], memory:'standard', settings:{release_floor:-66}});
  const plain = await patchFirmware(kit, off, stock);
  assert.deepEqual(imageSettings(kit, plain), {});
  assert.deepEqual(await recoverFirmware(kit, plain), {stock, selected:['a'], extensions:[], memory:'standard'});
});

test('two extensions may not choose the same capability setting', () => {
  const setting = {label:'Idle threshold', unit:'dB', minimum:-66, maximum:-20, default:-48};
  const idle = id => ({id, claims:[], actions:[], capabilities:['release_floor'], linked:false,
    settings:{release_floor:{...setting, value:-40}}});
  const kit = {extensions:[idle('quiet'), idle('quieter'), {id:'longer', claims:[], actions:[], capabilities:['long_samples'], linked:false}]};
  assert.equal(sharedClaim(kit.extensions), 'setting:release_floor');
  assert.throws(() => extensionSelection(kit, ['quiet', 'quieter']), /both use Idle threshold/);
  assert.deepEqual(extensionSelection(kit, ['quiet', 'longer']), ['quiet', 'longer']);
  // An extension that declares the capability without a value carries the default in its
  // metadata (tooling.extensions.metadata) and counts too, as check_collection has it.
  const plain = {id:'plainfloor', claims:[], actions:[], capabilities:['release_floor'], linked:false,
    settings:{release_floor:{...setting, value:setting.default}}};
  const mixed = {extensions:[idle('quiet'), plain]};
  assert.equal(sharedClaim(mixed.extensions), 'setting:release_floor');
  assert.throws(() => extensionSelection(mixed, ['quiet', 'plainfloor']), /both use Idle threshold/);
});

test('only an exact build restores its selection; others recover verified stock or are rejected', async () => {
  const {kit, linked, stock} = await profilingFixture();
  const on = await patchFirmware(kit, linked, stock), off = await patchFirmware(kit, linked, stock, {profiling:false});
  const earlier = {stock, selected:null, earlier:true, memory:'standard'};
  // Mixed modes, altered edits or altered linked code: stock is rebuilt from the
  // reviewed edits and verified by hash; the uploaded module is never used.
  const mixed = off.slice(); mixed.set(on.subarray(32, 40), 32);
  assert.deepEqual(await recoverFirmware(kit, mixed), earlier);
  for (const image of [on, off]) {
    for (const at of [32, 48, readLdr(image).at(-2).offset + 16]) {
      const damaged = image.slice(); damaged[at] ^= 1;
      assert.deepEqual(await recoverFirmware(kit, damaged), earlier);
    }
    // A change outside every reviewed edit is not OS 1.2.1.
    const foreign = image.slice(); foreign[60] ^= 1;
    await assert.rejects(recoverFirmware(kit, foreign), /not made from OS 1\.2\.1/);
  }
});

test('machines whose code lies within another present machine are tried without', () => {
  const text = (owner, bytes) => ({owner, relocations:[], sections:[{group:'text', index:1, size:bytes.length, data:Uint8Array.from(bytes)}]});
  const shared = Array.from({length:32}, (_, i) => i * 7 + 1);
  const kit = {objects:[text('big', shared), text('inside', shared.slice(8, 24)), text('other', shared.map(b => b ^ 0x55))]};
  // Only the machine whose largest code lies within another's is a lookalike.
  assert.deepEqual(presentSelections(kit, ['big', 'inside', 'other']), [['big', 'inside', 'other'], ['big', 'other']]);
  assert.deepEqual(presentSelections(kit, ['big', 'other']), [['big', 'other']]);
  // Identical code: each could be the one built, or both; the selection never empties.
  const twins = {objects:[text('a', shared), text('b', shared)]};
  assert.deepEqual(presentSelections(twins, ['a', 'b']), [['a', 'b'], ['b'], ['a']]);
  // Many lookalikes: fewest dropped first, up to the limit.
  const many = Array.from({length:9}, (_, i) => 'm' + i), tried = presentSelections({objects:many.map(id => text(id, shared))}, many);
  assert.equal(tried.length, RECOVERABLE_ALTERNATIVES);
  assert.deepEqual(tried.slice(0, 3), [many, many.slice(1), [many[0], ...many.slice(2)]]);
  assert.ok(tried.every((selection, i) => i === 0 || selection.length <= tried[i - 1].length));
});

test('an earlier release whose module loads elsewhere recovers stock OS 1.2.1', async () => {
  const {kit, linked, stock} = await profilingFixture();
  const image = await patchFirmware(kit, linked, stock, {profiling:false});
  // Move the module record's load address, as a release with smaller tables did.
  const at = readLdr(image).at(-2).offset, d = new DataView(image.buffer, image.byteOffset);
  d.setUint32(at + 4, kit.profile.module_base + 0x180000, true);
  image[at + 2] = 0; image[at + 2] = image.subarray(at, at + 16).reduce((a, b) => a ^ b, 0);
  assert.deepEqual(await recoverFirmware(kit, image), {stock, selected:null, earlier:true});
  // A kit too large to search every selection still recovers stock.
  kit.machines = [...kit.machines, ...Array.from({length:10}, (_, i) => machine('m' + i, 35 + i, 83 + i))];
  const exact = await patchFirmware(kit, linked, stock, {profiling:false});
  assert.deepEqual(await recoverFirmware(kit, exact), {stock, selected:null, earlier:true, memory:'standard'});
});

test('the experimental 4 MiB layout moves only the module base and the heap immediates', () => {
  const profile = JSON.parse(profileBytes), expanded = expandedProfile(profile);
  assert.equal(offersExpandedMemory(profile), true);
  assert.equal(expanded.reserved_bytes, FOUR_MIB);
  assert.equal(expanded.module_base, 0x8d0d0380);  // the same end as the reviewed reservation
  assert.equal(expanded.module_base + expanded.reserved_bytes, profile.module_base + profile.reserved_bytes);
  assert.equal(moduleBytes(expanded), 4080 * 1024);
  for (const [i, edit] of expanded.patches.entries()) {
    const reviewed = profile.patches[i];
    if (edit.kind !== 'heap') { assert.equal(edit, reviewed); continue; }
    // 20 MiB -> 16 MiB: the heap ends where the larger module begins.
    assert.deepEqual({...edit, name:undefined, after:undefined}, {...reviewed, name:undefined, after:undefined});
    assert.equal(edit.after, '01da000100000000');
    assert.equal(edit.name, 'IN-MEMORY application heap 0x1400000 -> 0x1000000 (4 MiB module reservation, experimental)');
    assert.equal(profile.heap_fill_base + 16 * 1024 ** 2, expanded.module_base);
  }
  assert.deepEqual(profile, JSON.parse(profileBytes));  // the reviewed profile is untouched
  assert.equal(Object.hasOwn(expanded, 'experimental_memory'), false);  // the layout offers no further one
  assert.equal(offersExpandedMemory(expanded), false);
  const kit = fixture();
  assert.equal(memoryKit(kit, false), kit);
  assert.equal(memoryKit(kit, true).profile.module_base, expanded.module_base);
  for (const value of [1, 'true', null, undefined]) assert.throws(() => memoryKit(kit, value), /must be a boolean/);
  // Edits that do not describe the reviewed heap cannot be widened.
  for (const change of [p => { p.patches = p.patches.filter(e => e.kind !== 'heap'); },
    p => { p.patches.find(e => e.kind === 'heap').after = '01da300100000000'; },
    p => { p.patches.find(e => e.kind === 'heap').before = 'e1a0400100000000'; },
    p => { p.heap_fill_bytes += 4; }, p => { p.reserved_bytes = FOUR_MIB; },
    p => { p.experimental_memory.module_base += 2; }, p => { p.experimental_memory.heap_after = '01dae00100000000'; },
    p => { p.experimental_memory.extra = 1; }, p => { delete p.experimental_memory; },
    // The larger heap must be loaded into the same register as the stock edit.
    p => { p.experimental_memory.heap_after = '02da000100000000'; },
    // A relative hook that cannot reach the lower module base.
    p => { p.experimental_memory.module_base -= 0x1000000; p.experimental_memory.reserved_bytes += 0x1000000;
      p.heap_fill_base -= 0x1000000; p.heap_fill_bytes += 0x1000000;
      p.patches.filter(e => e.kind === 'heap').forEach(e => Object.assign(e, {before:'01da400200000000', after:'01da200200000000'})); },
    // Lowercase hex only, as patching decodes it.
    p => { p.experimental_memory.heap_after = '01DA000100000000'; },
    // Sizes are whole MiB, as the builder names them.
    p => { p.experimental_memory.module_base += 0x4000; p.experimental_memory.reserved_bytes -= 0x4000;
      p.experimental_memory.heap_after = '01da000100400000'; },  // 16 MiB + 16 KiB, consistent otherwise
    // A reservation larger than the heap fill would leave no heap at all.
    p => Object.assign(p.experimental_memory, {module_base:p.heap_fill_base - 0x10000,
      reserved_bytes:p.module_base + p.reserved_bytes - p.heap_fill_base + 0x10000})]) {
    const altered = JSON.parse(profileBytes); change(altered);
    assert.throws(() => expandedProfile(altered), /cannot offer more memory|Unexpected heap edit/);
  }
});

// An authored loader whose heap fill holds the reservation, with the two heap
// edits and one hook, so a 4 MiB build is patched and recovered end to end.
async function heapFixture() {
  const kit = fixture(), stock = new Uint8Array(112), d = new DataView(stock.buffer);
  const record = (at, flags, address, size, argument = 0) => {
    [0xad000000 | flags, address, size, argument].forEach((v, i) => d.setUint32(at + i * 4, v, true));
    stock[at + 2] = stock.subarray(at, at + 16).reduce((a, b) => a ^ b, 0);
  };
  const heap = 0x800000, base = 0x100000, reserved = 2 * 1024 ** 2;
  record(0, 0x5004, 0x1000, 0, stock.length - 16);
  record(16, 4, 0x1000, 48);
  stock.set(Array.from({length:8}, (_, i) => i + 1), 32);
  const load = value => Buffer.from('01da' + Buffer.from(Uint16Array.of(value >>> 16, value & 0xffff).buffer).toString('hex') + '0000', 'hex');
  stock.set(load(heap), 48); stock.set(load(heap), 64);
  record(80, 0x104, base, heap);
  record(96, 0x8004, 0, 0);
  Object.assign(kit.profile, {stock_bytes:stock.length, stock_sha256:await sha256(stock), stock_records:4,
    module_base:base + heap - reserved, reserved_bytes:reserved, table_bytes:0, heap_fill_base:base, heap_fill_bytes:heap,
    experimental_memory:{module_base:base + heap - FOUR_MIB, reserved_bytes:FOUR_MIB, heap_after:load(heap - FOUR_MIB).toString('hex')},
    patches:[{kind:'hook', call:true, symbol:'entry', length:8, address:0x1000, file_offset:32,
      before:Buffer.from(stock.subarray(32, 40)).toString('hex')},
      ...[16, 32].map(offset => ({kind:'heap', name:'heap', length:8, address:0x1000 + offset, file_offset:32 + offset,
        before:load(heap).toString('hex'), after:load(heap - reserved).toString('hex')}))]});
  return {kit, stock, load};
}

test('a 4 MiB build loads its module lower, shrinks the heap and restores its selection', async () => {
  const {kit, stock, load} = await heapFixture();
  const standard = await patchFirmware(kit, linkKit(kit, ['a']), stock, {profiling:false});
  assert.deepEqual(await recoverFirmware(kit, standard), {stock, selected:['a'], extensions:[], memory:'standard'});
  const expanded = memoryKit(kit, true), linked = linkKit(expanded, ['a']);
  assert.equal(linked.sizes.available, FOUR_MIB - linked.sizes.allocated);
  const image = await patchFirmware(expanded, linked, stock, {profiling:false});
  const module = readLdr(image).at(-2);
  assert.equal(module.target, kit.profile.module_base + kit.profile.reserved_bytes - FOUR_MIB);
  const heap = module.target - kit.profile.heap_fill_base;
  for (const at of [48, 64]) assert.deepEqual(Buffer.from(image.subarray(at, at + 8)), load(heap));
  assert.notDeepEqual(image, standard);
  assert.deepEqual(await recoverFirmware(kit, image), {stock, selected:['a'], extensions:[], memory:'expanded'});
  // A module linked for one layout never passes as the other.
  const moved = image.slice(), at = module.offset, view = new DataView(moved.buffer);
  view.setUint32(at + 4, kit.profile.module_base, true);
  moved[at + 2] = 0; moved[at + 2] = moved.subarray(at, at + 16).reduce((a, b) => a ^ b, 0);
  assert.deepEqual(await recoverFirmware(kit, moved), {stock, selected:null, earlier:true});
  // A 4 MiB build this kit cannot rebuild still reports its memory choice.
  const other = image.slice(); other[module.offset + 16] ^= 1;
  assert.deepEqual(await recoverFirmware(kit, other), {stock, selected:null, earlier:true, memory:'expanded'});
  // A module at the 4 MiB base whose heap edits still write the standard size is no 4 MiB build.
  const standardHeap = other.slice();
  for (const at of [48, 64]) standardHeap.set(load(kit.profile.module_base - kit.profile.heap_fill_base), at);
  assert.deepEqual(await recoverFirmware(kit, standardHeap), {stock, selected:null, earlier:true});
  // A module linked for 4 MiB never patches into the standard layout.
  await assert.rejects(patchFirmware(kit, linked, stock), /Hook destination outside module/);
  // State that fits only the 4 MiB layout is refused by the standard one.
  const large = {...kit, objects:[...kit.objects, {...readObject(elf()), name:'b.o', owner:'a', symbols:[],
    relocations:[], sections:[{index:1, name:'.bss', group:'bss', type:8, alignment:4, size:3 * 1024 ** 2, data:new Uint8Array()}]}]};
  assert.throws(() => linkKit(large, ['a']), e => e.capacity === true && /need more than the 2048 KiB available/.test(e.message));
  const big = linkKit(memoryKit(large, true), ['a']);
  assert.ok(big.sizes.allocated > kit.profile.reserved_bytes);
  await assert.rejects(patchFirmware(kit, big, stock), /Module exceeds its profile/);
  assert.equal(readLdr(await patchFirmware(memoryKit(large, true), big, stock)).at(-2).target, module.target);
  // Recovery's search skips selections that cannot fit, here 'b' tried before 'a'.
  const order = {...large, machines:[machine('b', 35, 83), ...kit.machines],
    objects:large.objects.map(o => o.name === 'b.o' ? {...o, owner:'b'} : o)};
  const small = await patchFirmware(order, linkKit(order, ['a']), stock, {profiling:false});
  assert.deepEqual(await recoverFirmware(order, small), {stock, selected:['a'], extensions:[], memory:'standard'});
  // Layouts record the choice; readLayout reports whether this site offers it.
  const size = FOUR_MIB, layoutOf = extra => ({format:'opxy-layout/1', profile:kit.profile.id, machines:{}, ...extra});
  assert.deepEqual(readLayout(kit, layoutOf({memory:'expanded', reserved_bytes:size})), {machines:{}, selected:null, expanded:true});
  // A layout from another experimental size is not taken for this one.
  assert.deepEqual(readLayout(kit, layoutOf({memory:'expanded', reserved_bytes:2 * 1024 ** 2})), {machines:{}, selected:null, memoryUnavailable:2 * 1024 ** 2});
  assert.deepEqual(readLayout(kit, layoutOf({memory:'expanded', reserved_bytes:6 * 1024 ** 2})), {machines:{}, selected:null, memoryUnavailable:6 * 1024 ** 2});
  for (const bad of [{memory:'expanded'}, {reserved_bytes:size}, {memory:'expanded', reserved_bytes:'4'},
    {memory:'expanded', reserved_bytes:5}, {memory:'expanded', reserved_bytes:0}, {memory:'expanded', reserved_bytes:size + 4}])
    assert.throws(() => readLayout(kit, layoutOf(bad)), /Invalid memory/);
  assert.deepEqual(readLayout(kit, {format:'opxy-layout/1', profile:kit.profile.id, machines:{}}), {machines:{}, selected:null});
  assert.throws(() => readLayout(kit, {format:'opxy-layout/1', profile:kit.profile.id, machines:{}, memory:'large'}), /Invalid memory/);
  // A site without the option keeps the layout's numbers and reports the choice it left out.
  const plain = fixture(); delete plain.profile.experimental_memory;
  assert.deepEqual(readLayout(plain, {format:'opxy-layout/1', profile:plain.profile.id, machines:{a:{engine:34,page:82}}, memory:'expanded', reserved_bytes:FOUR_MIB}),
    {machines:{a:{engine:34,page:82}}, selected:null, memoryUnavailable:FOUR_MIB});
  assert.throws(() => memoryKit(plain, true), /cannot offer more memory/);
});

test('heap voices link with standard memory only', async () => {
  const {kit} = await heapFixture();
  const heavy = {...kit, machines:kit.machines.map(m => m.id === 'a' ? {...m, heapVoices:147567} : m)};
  assert.ok(linkKit(heavy, ['a']).sizes.allocated > 0);
  assert.throws(() => linkKit(memoryKit(heavy, true), ['a']),
    e => e.heapVoices === true && e.capacity === undefined && /stock heap.*standard memory/.test(e.message));
  // Without heap voices the experimental memory links as before.
  assert.ok(linkKit(memoryKit(kit, true), ['a']).sizes.allocated > 0);
  assert.equal(memoryKit(kit, true).experimental, true);
  assert.equal(kit.experimental, undefined);
});

test('profiling option and profile hook condition must be exact booleans', async () => {
  const {kit, linked, stock} = await profilingFixture();
  for (const profiling of [0, 1, 'false', 'true', null, [], {}])
    await assert.rejects(patchFirmware(kit, linked, stock, {profiling}), /Profiling must be a boolean/);
  for (const profiling of [true, 0, 'false', null]) {
    kit.profile.patches[0].profiling = profiling;
    await assert.rejects(patchFirmware(kit, linked, stock), /Invalid profiling hook/);
  }
  kit.profile.patches[0].profiling = false;
  kit.profile.patches[0].kind = 'table';
  await assert.rejects(patchFirmware(kit, linked, stock), /Invalid profiling hook/);
});

test('inactive profiling hooks retain stock mapping and overlap guards', async () => {
  const {kit, linked, stock} = await profilingFixture();
  kit.profile.patches[0].file_offset++;
  await assert.rejects(patchFirmware(kit, linked, stock), /Patch preimage\/mapping mismatch/);
  kit.profile.patches[0].file_offset--;
  kit.profile.patches.push({...kit.profile.patches[0]});
  await assert.rejects(patchFirmware(kit, linked, stock), /Overlapping patch intervals/);
});
test('ELF parsing reads symbols and little-endian instructions', () => {
  const o = readObject(elf()); assert.equal(o.symbols[1].name,'entry'); assert.equal(o.symbols[1].size,4);
  assert.deepEqual([...o.sections[1].data],[0,96,16,0]);
});
test('runtime objects enter only selections that import their arithmetic helpers', () => {
  const kit = fixture(), baseline = linkKit(kit, ['a']);
  kit.machines.push(machine('b', 35, 85));
  const consumer = {...readObject(elf()), name:'b.o', owner:'b'};
  consumer.symbols[1].name = 'b_entry';
  consumer.symbols.push({name:'___muldi3', binding:1, section:0, type:2, value:0, size:0});
  const helper = {...readObject(elf()), name:'sdk/runtime/_muldi3.o', owner:null, runtime:true};
  helper.symbols[1].name = '___muldi3';
  kit.objects.push(consumer, helper);
  const integers = linkKit(kit, ['a']), arithmetic = linkKit(kit, ['b']);
  assert.deepEqual(integers.module, baseline.module);
  assert.deepEqual(integers.objects.map(o => o.name), ['a.o']);
  assert.deepEqual(arithmetic.objects.map(o => o.name), ['b.o', 'sdk/runtime/_muldi3.o']);
  assert(arithmetic.globals.has('___muldi3'));
});
test('truncated sections, wrong architectures, merge flags, constructors and COMMON fail closed', () => {
  assert.throws(()=>readObject(elf().slice(0,200)),/section table/);
  for (const [offset, type, value] of [[18,16,3],[128+40+8,32,0x36],[128+40+16,32,9999],[86,16,0xfff2]]) {
    const b=elf(), d=new DataView(b.buffer); d[type===16?'setUint16':'setUint32'](offset,value,true);
    assert.throws(()=>readObject(b));
  }
  const b=elf(); b.set(Buffer.from('.ctor'),97); assert.throws(()=>readObject(b),/Unsupported allocated section/);
});
test('exact deterministic section placement and immutable input capsules', () => {
  const k=fixture(), linked=linkKit(k,['a']);
  assert.equal(linked.globals.get('entry'),k.profile.module_base);
  assert.deepEqual(linked.sizes,{initialized:4,bss:0,allocated:4,available:moduleBytes(k.profile)-4});
  assert.deepEqual(linkKit(k,['a']).module,linked.module);
});

test('extensions omit code/state and resolve only shared registry imports to an absent accessor', () => {
  const k = fixture();
  k.extensions = [{id:'capture', claims:['record_double_tap'], actions:['edit_notes']}];
  k.definitions.kit_absent = k.profile.module_base;
  const extension = {...readObject(elf()), name:'extension.o', owner:null, extension:'capture'};
  extension.symbols[1].name = 'sdk_extension_capture_hooks';
  extension.sections.push({index:5, name:'.bss', group:'bss', type:8, size:128, alignment:4});
  const registry = k.objects[0]; registry.owner = null;
  registry.symbols.push({name:'sdk_extension_capture_hooks', section:0, value:0, size:0, binding:1, type:0});
  registry.relocations.push({section:1, offset:0, type:18, symbol:2, addend:0});
  k.objects.push(extension);
  const on = linkKit(k, ['a'], null, ['capture']), off = linkKit(k, ['a'], null, []);
  assert.deepEqual(linkKit(k, ['a']).module, off.module);  // omitted means none
  assert.equal(on.sizes.bss, 128); assert.equal(off.sizes.bss, 0);
  assert.equal(off.objects.length, 1); assert.equal(on.objects.length, 2);
  assert.notDeepEqual(on.module, off.module);
  assert.equal(off.aliases.get('sdk_extension_capture_hooks'), k.definitions.kit_absent);
  assert.deepEqual(linkKit(k, ['a'], null, ['capture']).module, on.module);
  registry.owner = 'a';
  assert.throws(() => linkKit(k, ['a'], null, []), /Unresolved import/);
});

test('master processors are independently selectable and omitted accessors resolve safely', () => {
  const k=fixture();k.definitions.kit_absent=k.profile.module_base;
  k.extensions=['first','second'].map(id=>({id,claims:[],actions:[],master_processor:{state_id:id}}));
  assert.deepEqual(extensionSelection(k,['second','first']),['first','second']);
  const registry=k.objects[0];registry.owner=null;
  for(const id of ['first','second']) {
    const object={...readObject(elf()),name:id+'.o',owner:null,extension:id};
    object.symbols[1].name='sdk_extension_'+id+'_hooks';
    for(const suffix of ['processor','panel'])object.symbols.push({...object.symbols[1],name:'sdk_extension_'+id+'_'+suffix});
    object.sections.push({index:5,name:'.bss',group:'bss',type:8,size:128,alignment:4});
    k.objects.push(object);
    for(const suffix of ['hooks','processor','panel'])registry.symbols.push({name:'sdk_extension_'+id+'_'+suffix,section:0,value:0,size:0,binding:1,type:0});
  }
  const both=linkKit(k,['a'],null,['second','first']),one=linkKit(k,['a'],null,['second']),none=linkKit(k,['a'],null,[]);
  assert.equal(both.sizes.bss,256);assert.equal(one.sizes.bss,128);assert.equal(none.sizes.bss,0);
  for(const suffix of ['hooks','processor','panel']) {
    assert.equal(one.aliases.get('sdk_extension_first_'+suffix),k.definitions.kit_absent);
    assert.equal(one.aliases.has('sdk_extension_second_'+suffix),false);
  }
});

test('extension selections preserve collection order, empty choices and exclusive gestures', () => {
  const k = {extensions:[{id:'observer', claims:[]}, {id:'capture', claims:['record_double_tap']}, {id:'replace', claims:['record_double_tap']}]};
  assert.deepEqual(extensionSelection(k, []), []);
  assert.deepEqual(extensionSelection(k, ['capture','observer']), ['observer','capture']);
  assert.throws(() => extensionSelection(k, ['capture','replace']), /record double tap/);
  assert.deepEqual(extensionChoices(k), [[], ['observer'], ['capture'], ['observer','capture'], ['replace'], ['observer','replace']]);
  for (const selection of [['missing'], ['capture','capture'], null, 'capture'])
    assert.throws(() => extensionSelection(k, selection), /available extension/);
  assert.deepEqual(extensionSelection({}), []);
});

test('one selected extension owns stock Drumline\'s second page, as a claim', () => {
  // tooling.extensions.metadata marks a drumline_page extension; check_collection allows one per build.
  const k = {extensions:[{id:'filters', claims:[], drumline_page:true}, {id:'observer', claims:[]},
    {id:'grit', claims:['record_double_tap'], claim_labels:{record_double_tap:'Record double-tap'}, drumline_page:true}]};
  assert.equal(sharedClaim([k.extensions[0], k.extensions[1]]), null);
  assert.equal(sharedClaim([k.extensions[0], k.extensions[2]]), 'drumline_page');
  assert.equal(claimLabel(k.extensions, 'drumline_page'), 'Drumline page 2');
  assert.equal(claimLabel(k.extensions, 'record_double_tap'), 'Record double-tap');
  assert.deepEqual(extensionSelection(k, ['observer','filters']), ['filters','observer']);
  assert.throws(() => extensionSelection(k, ['grit','filters']), /both use Drumline page 2\. Choose only one\./);
  assert.deepEqual(extensionChoices(k), [[], ['filters'], ['observer'], ['filters','observer'], ['grit'], ['observer','grit']]);
});

// The mixer (service mixer) is shared by the per-key sends (drum_sends) and Sample Studio's player
// (sample_studio): its object calls both users' entry points, and a selection with one user links the
// other's to kit_absent, as tooling.native.service_fallbacks lists them.
function mixerKit() {
  const k = fixture();
  k.definitions.kit_absent = k.profile.module_base;
  const sdk = (name, service, exports, imports = []) => {
    const o = {...readObject(elf()), name, owner:null, service};
    o.symbols = o.symbols.filter(s => s.name !== 'entry');
    for (const n of exports) o.symbols.push({name:n, section:1, value:0, size:0, binding:1, type:2});
    for (const n of imports) {
      o.symbols.push({name:n, section:0, value:0, size:0, binding:1, type:0});
      o.relocations.push({section:1, offset:0, type:18, symbol:o.symbols.length - 1, addend:0});
    }
    return o;
  };
  const sends = ['sdk_drum_send_begin', 'sdk_drum_send_voice', 'sdk_drum_send_gain', 'sdk_drum_send_own_mix'];
  const studio = ['sdk_studio_mixer_begin', 'sdk_studio_voice', 'sdk_studio_gain', 'sdk_studio_level'];
  k.objects.push(sdk('sdk/mixer.o', 'mixer', ['sdk_mixer_voice'], [...sends, ...studio]),
    sdk('sdk/drum_send.o', 'drum_sends', sends), sdk('sdk/sample_studio_audio.o', 'sample_studio', studio));
  k.serviceFallbacks = {drum_sends:Object.fromEntries(sends.map(n => [n, 'kit_absent'])),
    sample_studio:Object.fromEntries(studio.map(n => [n, 'kit_absent']))};
  k.extensions = [{id:'pages', claims:[], actions:[], drumline_page:true},
    {id:'studio', claims:[], actions:[], sample_studio:{asset:'sample_studio.svg', artwork:'', sha256:''}}];
  return {k, sends, studio};
}
const linkedNames = linked => linked.objects.map(o => o.name);
test('the mixer links with only the per-key sends: Sample Studio\'s player resolves to kit_absent', () => {
  const {k, sends, studio} = mixerKit();
  k.machines[0].features = ['padMix'];
  const padMix = linkKit(k, ['a']);
  assert.ok(padMix.services.has('mixer') && padMix.services.has('drum_sends') && !padMix.services.has('sample_studio'));
  assert.deepEqual(linkedNames(padMix), ['a.o', 'sdk/mixer.o', 'sdk/drum_send.o']);
  for (const n of studio) assert.equal(padMix.aliases.get(n), k.definitions.kit_absent);
  for (const n of sends) assert.equal(padMix.aliases.has(n), false);
  delete k.machines[0].features;
  const pages = linkKit(k, ['a'], null, ['pages']);   // a drumline_page extension and no padMix kit
  assert.ok(pages.services.has('mixer') && pages.services.has('drum_sends') && !pages.services.has('sample_studio'));
  assert.deepEqual(linkedNames(pages), ['a.o', 'sdk/mixer.o', 'sdk/drum_send.o']);
  for (const n of studio) assert.equal(pages.aliases.get(n), k.definitions.kit_absent);
  // Without the fallbacks the player's entry points stay unresolved.
  delete k.serviceFallbacks.sample_studio;
  assert.throws(() => linkKit(k, ['a'], null, ['pages']), /Unresolved import/);
});
test('the mixer links with only Sample Studio: the sends resolve to kit_absent, and it needs a sample engine', () => {
  const {k, sends, studio} = mixerKit();
  assert.throws(() => linkKit(k, ['a'], null, ['studio']), /Sample Studio needs a sample engine or Drumline page 2/);
  k.machines[0].kind = 'sample';
  const alone = linkKit(k, ['a'], null, ['studio']);
  for (const service of ['sample_studio', 'master_bus', 'mixer', 'samples']) assert.ok(alone.services.has(service), service);
  assert.equal(alone.services.has('drum_sends'), false);
  assert.deepEqual(linkedNames(alone), ['a.o', 'sdk/mixer.o', 'sdk/sample_studio_audio.o']);
  for (const n of sends) assert.equal(alone.aliases.get(n), k.definitions.kit_absent);
  for (const n of studio) assert.equal(alone.aliases.has(n), false);
  // Neither user: the mixer isn't linked at all.
  assert.deepEqual(linkedNames(linkKit(k, ['a'])), ['a.o']);
  // The device identity beside the studio: their B3 parameters are apart (138..145, 146..160).
  k.machines[0].features = ['deviceIdentity'];
  assert.ok(linkKit(k, ['a'], null, ['studio']).services.has('device_identity'));
});
// Sample Studio beside a master processor (a collection with both): the master bus's shared handler
// (sdk/master_bus.o, service master_bus) enters each side's entries, and a selection with one of them
// links the other's to kit_absent, as tooling.native.service_fallbacks lists them with master_shared.
test('the master bus links one shared handler for the studio and a master processor, or either alone', () => {
  const {k} = mixerKit();
  k.machines[0].kind = 'sample';
  const sdk = (name, service, exports, imports = []) => {
    const o = {...readObject(elf()), name, owner:null, service};
    o.symbols = o.symbols.filter(s => s.name !== 'entry');
    for (const n of exports) o.symbols.push({name:n, section:1, value:0, size:0, binding:1, type:2});
    for (const n of imports) {
      o.symbols.push({name:n, section:0, value:0, size:0, binding:1, type:0});
      o.relocations.push({section:1, offset:0, type:18, symbol:o.symbols.length - 1, addend:0});
    }
    return o;
  };
  const processor = ['sdk_processor_process', 'sdk_processor_receive'], player = ['sdk_studio_master', 'sdk_studio_receive'];
  k.objects = k.objects.filter(o => o.name !== 'sdk/sample_studio_audio.o');
  k.objects.push(sdk('sdk/master_filter_stock.o', 'master_filter', processor),
    sdk('sdk/sample_studio_audio.o', 'sample_studio', [...player, 'sdk_studio_mixer_begin', 'sdk_studio_voice', 'sdk_studio_gain', 'sdk_studio_level']),
    sdk('sdk/master_bus.o', 'master_bus', ['sdk_master_process', 'sdk_master_receive'], [...processor, ...player]));
  k.serviceFallbacks.master_filter = Object.fromEntries(processor.map(n => [n, 'kit_absent']));
  Object.assign(k.serviceFallbacks.sample_studio, Object.fromEntries(player.map(n => [n, 'kit_absent'])));
  k.extensions.push({id:'filter', claims:[], actions:[], master_processor:{state_id:'filter'}});
  const sites = ['0x8d511b80', '0x8d5b145e'].map(a => k.profile.patches.find(p => p.address === a));
  const both = linkKit(k, ['a'], null, ['studio', 'filter']);
  for (const service of ['sample_studio', 'master_filter', 'master_bus', 'parameter_receive']) assert.ok(both.services.has(service), service);
  assert.ok(linkedNames(both).includes('sdk/master_bus.o'));
  for (const n of [...processor, ...player]) assert.equal(both.aliases.has(n), false, n);
  assert.deepEqual(sites.map(p => hookSymbol(p, both.services)), ['sdk_master_process', 'sdk_master_receive']);
  assert.equal(both.globals.get('sdk_master_process'), both.globals.get('sdk_master_receive'));   // master_bus.o's
  const studio = linkKit(k, ['a'], null, ['studio']);
  assert.ok(linkedNames(studio).includes('sdk/master_bus.o') && !linkedNames(studio).includes('sdk/master_filter_stock.o'));
  for (const n of processor) assert.equal(studio.aliases.get(n), k.definitions.kit_absent, n);
  const filter = linkKit(k, ['a'], null, ['filter']);
  assert.ok(linkedNames(filter).includes('sdk/master_bus.o') && !linkedNames(filter).includes('sdk/sample_studio_audio.o'));
  for (const n of player) assert.equal(filter.aliases.get(n), k.definitions.kit_absent, n);
  // With the device identity too, its receiver runs first and hands the shared handler the rest.
  k.machines[0].features = ['deviceIdentity'];
  const all = linkKit(k, ['a'], null, ['studio', 'filter']);
  assert.ok(all.services.has('device_identity'));
  assert.equal(hookSymbol(sites[1], all.services), 'sdk_identity_receive');
});
test('one selected extension owns Sample Studio, beside a master processor if chosen', () => {
  const studio = {asset:'sample_studio.svg', artwork:'', sha256:''};
  const k = {extensions:[{id:'studio', claims:[], sample_studio:studio}, {id:'other', claims:[], sample_studio:studio},
    {id:'filter', claims:[], master_processor:{state_id:'filter'}}, {id:'observer', claims:[]}]};
  assert.equal(sharedClaim([k.extensions[0], k.extensions[3]]), null);
  assert.equal(sharedClaim([k.extensions[0], k.extensions[1]]), 'sample_studio');
  assert.equal(sharedClaim([k.extensions[0], k.extensions[2]]), null);   // one handler runs both (master_bus.cpp)
  assert.equal(claimLabel(k.extensions, 'sample_studio'), 'Sample Studio');
  assert.throws(() => extensionSelection(k, ['studio','other']), /both use Sample Studio\. Choose only one\./);
  assert.deepEqual(extensionSelection(k, ['filter','studio']), ['studio','filter']);
  assert.deepEqual(extensionSelection(k, ['observer','studio']), ['studio','observer']);
  for (const choice of extensionChoices(k)) {
    assert.ok(choice.filter(id => id === 'studio' || id === 'other').length <= 1, choice.join());
  }
  assert.ok(extensionChoices(k).some(choice => choice.includes('filter') && choice.includes('studio')));
});

test('shared objects name no machine: an unselected machine\'s symbols stay unresolved', () => {
  const k=fixture(), o=k.objects[0];
  k.definitions.fallback=k.profile.module_base;
  k.machines.push({id:'missing',prefix:'probe',kind:'sample',identity:{engine:46,page:7}});
  o.symbols.push({name:'probe_ready',section:0,value:0,size:0,binding:1,type:0});
  o.relocations.push({section:1,offset:0,type:18,symbol:2,addend:0});
  for (const owner of [null, 'a']) { o.owner=owner; assert.throws(()=>linkKit(k,['a']),/Unresolved import: probe_ready/); }
});
test('the SDK\'s machine tables link the selection\'s records, by kind then ID', () => {
  const k=fixture(), o=k.objects[0];
  k.machines.push({...machine('c',35,83),kind:'effect',identity:{effect:21,page:21}}, machine('b',36,84));
  for (const id of ['b','c']) k.objects.push({...readObject(elf(id+'_record')),name:id+'.o',owner:id});
  o.symbols[1].name='a_record';
  const linked=linkKit(k,['c','a']);
  assert.equal(linked.objects.some(x=>x.name===RECORDS_OBJECT),false);  // nothing reads them
  o.symbols.push({name:'sdk_machine_records',section:0,value:0,size:0,binding:1,type:0});
  o.relocations.push({section:1,offset:0,type:18,symbol:2,addend:0});
  const records=linkKit(k,['c','a']), names=records.objects.map(x=>x.name);
  assert.deepEqual(names,['a.o','c.o',RECORDS_OBJECT]);
  const object=records.objects.at(-1);
  assert.deepEqual(object.elf,recordsObject(k.machines.filter(m=>['a','c'].includes(m.id))));
  assert.deepEqual(recordOrder(k.machines).map(m=>m.id),['a','b','c']);
  // Its count, its space and the records, which the selection's own objects define.
  const at=records.globals.get('sdk_machine_records'), base=k.profile.module_base;
  const word=address=>new DataView(records.module.buffer,records.module.byteOffset).getUint32(address-base,true);
  assert.deepEqual([word(at-8),word(at-4),word(at),word(at+4)],
    [2,3*RECORD_TABLE_BYTES,records.globals.get('a_record'),records.globals.get('c_record')]);
  assert.equal(records.sizes.bss,3*RECORD_TABLE_BYTES);
});
test('SDK objects of a profile service link only when a selected machine uses it', () => {
  const k=fixture();
  assert.deepEqual([...activeServices(k.profile,[{kind:'synth'},{kind:'effect'}])],[]);
  assert.deepEqual([...activeServices(k.profile,[{kind:'synth'},{kind:'sample'}])],['samples', 'octaves']);
  // The service object redefines `entry`, so linking it would be a duplicate global.
  k.objects.push({...k.objects[0],name:'service.o',owner:null,service:'samples'});
  const linked=linkKit(k,['a']);
  assert.deepEqual(linked.objects.map(o=>o.name),['a.o']); assert.equal(linked.services.size,0);
  k.machines.push({...machine('b',46,7),kind:'sample'});
  assert.throws(()=>linkKit(k,['a','b']),/Duplicate global symbol: entry/);
});
test('track control and hidden pages use the control service, whose entry points fall back while unused', () => {
  const k=fixture(), o=k.objects[0];
  assert.deepEqual([...activeServices(k.profile,[{kind:'synth',features:['trackControl']}])],['control']);
  assert.deepEqual([...activeServices(k.profile,[{kind:'synth',features:['hidePages']}])],['control']);
  assert.deepEqual([...activeServices(k.profile,[{kind:'synth'}])],[]);
  // Shared startup calls the control service's saved data, which only a control build links.
  k.definitions.fallback=k.profile.module_base;
  o.owner=null;
  o.symbols.push({name:'sdk_control_kept_write',section:0,value:0,size:0,binding:1,type:0});
  o.relocations.push({section:1,offset:0,type:18,symbol:o.symbols.length-1,addend:0});
  assert.throws(()=>linkKit(k,['a']),/Unresolved import/);
  k.serviceFallbacks={control:{sdk_control_kept_write:'fallback'}};
  assert.doesNotThrow(()=>linkKit(k,['a']));
  k.machines[0].features=['trackControl'];
  assert.throws(()=>linkKit(k,['a']),/Unresolved import/);
  k.serviceFallbacks={control:{sdk_control_kept_write:'undefined_fallback'}}; delete k.machines[0].features;
  assert.throws(()=>linkKit(k,['a']),/Invalid service fallback/);
});
test('empty, unknown, repeated selections, duplicate globals and memory exhaustion are rejected', () => {
  const k=fixture(); for(const s of [[],['missing'],['a','a']]) assert.throws(()=>linkKit(k,s),/Choose each machine once/);
  k.definitions.entry=42; assert.throws(()=>linkKit(k,['a']),/Duplicate global/); delete k.definitions.entry;
  k.objects[0].sections[1].size=k.profile.reserved_bytes; assert.throws(()=>linkKit(k,['a']),/need more than the/);
});
test('code, data and state share one module limit that names each machine', () => {
  const k=fixture(), tables=k.profile.table_bytes;
  k.profile.table_bytes=k.profile.reserved_bytes-3; assert.throws(()=>linkKit(k,['a']),/KiB available: a /);
  k.profile.table_bytes=tables;
  k.objects[0].sections.push({index:5,name:'.bss',group:'bss',type:8,size:moduleBytes(k.profile),alignment:4});
  assert.throws(()=>linkKit(k,['a']),/need more than the/);
});
// tests/test_profile_parity.py checks validIdentity against the Python rules.
test('machine numbering cases shared with tooling.machines', () => {
  const cases=JSON.parse(readFileSync(new URL('./fixtures/numbers.json',import.meta.url))), rules=JSON.parse(profileBytes).identities;
  const fixed=new Set(cases.fixed.map(([id])=>id));
  for (const c of cases.cases) {
    const run=()=>numberLayout(rules,cases.fixed,c.layout??null,c.machines);
    if (c.error) { assert.throws(run,new RegExp(c.error),c.name); continue; }
    assert.deepEqual(Object.fromEntries(Object.entries(run()).filter(([id])=>!fixed.has(id))),c.numbers,c.name);
  }
});
test('numbers follow the layout, not the selection order, and unselected machines link as 0', () => {
  const k=fixture(); k.machines.push(machine('b',34,82,'personal'),machine('c',34,82,'personal'));
  assert.deepEqual(assignNumbers(k,['c','b','a']),assignNumbers(k,['a','b','c']));
  // c's own places in the pools (FNV-1a of its ID), as it asks for a's fixed numbers.
  assert.deepEqual(assignNumbers(k,['a','c']).c,{engine:246,page:204});
  // Once b has numbers, they stay held when b is dropped; c keeps its own, which b's do not collide with.
  const numbers=assignNumbers(k,['a','b']);
  assert.deepEqual(numbers.b,{engine:215,page:221});
  assert.deepEqual(assignNumbers(k,['a','c'],numbers),{...numbers,c:{engine:246,page:204}});
  const linked=linkKit(k,['a','c'],numbers);
  assert.deepEqual([linked.definitions.a_engine_id,linked.definitions.b_engine_id,linked.definitions.c_engine_id,
    linked.definitions.c_page_id],[34,0,246,204]);
  assert.deepEqual(linked.numbers.b,{engine:215,page:221});
});
test("a sample engine drawing its own screen links its page apart from the sampler's", () => {
  const k=fixture(); k.machines.push({id:'g',prefix:'g',kind:'sample',identity:{engine:null,page:null},origin:'personal'});
  const numbers=assignNumbers(k,['a','g']);
  assert.ok(numbers.g.page>81 && numbers.g.page<256);
  const linked=linkKit(k,['a','g'],numbers).definitions;
  assert.deepEqual([linked.g_engine_id,linked.g_page_id,linked.g_screen_page_id],[numbers.g.engine,2,numbers.g.page]);
  const absent=linkKit(k,['a'],numbers).definitions;
  assert.deepEqual([absent.g_engine_id,absent.g_page_id,absent.g_screen_page_id],[0,0,0]);
});
test('saved layouts restore numbers and selections, including older saved selections', () => {
  const k=fixture(), id=k.profile.id;
  assert.deepEqual(readLayout(k,{format:'opxy-layout/1',profile:id,selected:['a'],machines:{a:{engine:34,page:82},gone:{engine:40,page:90}}}),
    {selected:['a'],machines:{a:{engine:34,page:82},gone:{engine:40,page:90}}});
  assert.deepEqual(readLayout(k,{format:'opxy-kit/1',profile:id,selected:['a'],identities:{a:{page:82,engine:34}}}).machines,{a:{page:82,engine:34}});
  assert.throws(()=>readLayout(k,{format:'opxy-layout/1',profile:'wrong',machines:{}}),/different OS version/);
  assert.deepEqual(readLayout(k,{format:'opxy-layout/1',profile:k.profile.layout_profiles[0],machines:{}}).machines,{});
  assert.throws(()=>readLayout(k,{format:'opxy-layout/1',profile:id,selected:['missing'],machines:{}}),/doesn’t have/);
  assert.throws(()=>readLayout(k,{format:'opxy-layout/1',profile:id,machines:{a:{engine:'34',page:82}}}),/Invalid numbers/);
  assert.throws(()=>linkKit(k,['a'],{a:{engine:40,page:90}}),/a has fixed numbers/);
});
test('undefined imports never silently become zero', () => {
  const k=fixture(), o=k.objects[0];
  o.symbols.push({name:'unknown',section:0,value:0,size:0,binding:1,type:0});
  o.relocations.push({section:1,offset:0,type:18,symbol:2,addend:0});
  assert.throws(()=>linkKit(k,['a']),/Unresolved import/);
});
test('full-width pointer and high/low immediate relocation', () => {
  for(const [type, offset, expected] of [[18,0,[0x78,0x56,0x34,0x12]],[6,0,[0x78,0x56,16,0]],[7,0,[0x34,0x12,16,0]]]) {
    const k=fixture(), o=k.objects[0]; k.definitions.target=0x12345678;
    o.symbols.push({name:'target',section:0,value:0,size:0,binding:1,type:0});
    o.relocations.push({section:1,offset,type,symbol:2,addend:0});
    assert.deepEqual([...linkKit(k,['a']).module],expected);
  }
});
test('call relocation uses the instruction start and rejects odd or out-of-range destinations', () => {
  const k=fixture(), o=k.objects[0]; o.sections[1].data=new Uint8Array([0,0xe3,0,0]);
  o.symbols.push({name:'target',section:0,value:0,size:0,binding:1,type:0});
  o.relocations.push({section:1,offset:2,type:10,symbol:2,addend:0});
  k.definitions.target=k.profile.module_base+10;
  assert.deepEqual([...linkKit(k,['a']).module],[0,0xe3,5,0]);
  k.definitions.target++; assert.throws(()=>linkKit(k,['a']),/relative branch/);
  k.definitions.target=1; assert.throws(()=>linkKit(k,['a']),/relative branch/);
});
test('curated profile and object authentication reject modified input', async () => {
  const k=fixture(); k.format='opxy-browser-kit/1'; k.profileSource=profileBytes.toString('base64');
  k.objects=[{name:'a.o',owner:'a',bytes:Buffer.from(elf()).toString('base64'),sha256:await sha256(elf())}];
  k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg',import.meta.url),'utf8');
  await openKit(k);
  const template=k.screenTemplate;
  for(const changed of [template.replace('id="values"','id="wrong"'),null,'']) {
    k.screenTemplate=changed;
    await assert.rejects(openKit(k),/screen template/);
  }
  k.screenTemplate=template;
  delete k.stockCatalog;
  await assert.rejects(openKit(k),/Missing bundled stock menu/);
  k.stockCatalog='';
  for (const version of [undefined, '1.0', 'v0.0.2']) {
    k.version=version; await assert.rejects(openKit(k),/kit version/);
  }
  k.version='0.0.2';
  k.objects[0].sha256='0'.repeat(64); await assert.rejects(openKit(k),/hash mismatch/);
  k.profile.patches[0].after='0000000000000000'; await assert.rejects(openKit(k),/reviewed profile/);
});
test('a Sample Studio extension carries its artwork, checked by its hash, and is never a master processor', async () => {
  const k=fixture(); k.format='opxy-browser-kit/1'; k.profileSource=profileBytes.toString('base64');
  k.objects=[{name:'a.o',owner:'a',bytes:Buffer.from(elf()).toString('base64'),sha256:await sha256(elf())}];
  k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg',import.meta.url),'utf8');
  const artwork=Buffer.from('<svg xmlns="http://www.w3.org/2000/svg"/>');
  const studio={asset:'sample_studio.svg', artwork:artwork.toString('base64'), sha256:await sha256(artwork)};
  k.extensions=[{id:'studio', name:'Sample Studio', description:'An editor.', origin:'personal', claims:[], actions:[],
    claim_labels:{}, sample_studio:studio}];
  // Its metadata passes; the kit's next check is the extension's object, which this fixture leaves out.
  await assert.rejects(openKit(k), /Missing extension accessor: studio/);
  for (const changed of [{...studio, sha256:'0'.repeat(64)}, {...studio, asset:'other.svg'}, {...studio, extra:1}, true]) {
    k.extensions[0].sample_studio=changed;
    await assert.rejects(openKit(k), /Sample Studio/);
  }
  k.extensions[0].sample_studio=studio;
  k.extensions[0].linked=false;
  await assert.rejects(openKit(k), /Invalid extension/);
});
test('SDK 1.5 machines ask for no numbers and the selection gives them', async () => {
  const k=fixture(); k.format='opxy-browser-kit/1'; k.profileSource=profileBytes.toString('base64');
  k.objects=[{name:'a.o',owner:'a',bytes:Buffer.from(elf()).toString('base64'),sha256:await sha256(elf())}];
  k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg',import.meta.url),'utf8');
  // a has fixed numbers yet asks for none; a sample engine names only its base's page.
  k.machines=[{...machine('a',null,null)},{...machine('b',null,null)},
    {id:'d',prefix:'d',kind:'sample',identity:{engine:null,page:7},origin:'personal'},
    {id:'e',prefix:'e',kind:'effect',identity:{effect:null,page:21},origin:'personal'}];
  await openKit(k);
  assert.deepEqual(assignNumbers(k,['a','b','d','e']),
    {a:{engine:34,page:82},b:{engine:215,page:221},d:{engine:179,page:7},e:{effect:47,page:21}});
  assert.deepEqual(assignNumbers(k,['b'],{b:{engine:50,page:100}}).b,{engine:50,page:100});
  for (const identity of [{engine:null,page:82},{engine:null,page:null,base:'sampler'}]) {
    k.machines[1].identity=identity; await assert.rejects(openKit(k),/Machine numbers/);
  }
  k.machines[1].identity={engine:null,page:null}; k.machines[2].identity={engine:null,page:21};
  await assert.rejects(openKit(k),/Machine numbers/);
  k.machines[2].identity={engine:null,page:7}; k.machines[0].identity={engine:34,page:83};
  await assert.rejects(openKit(k),/Fixed machine numbers differ/);
});
test('a machine says where it comes from: a catalog release or a personal folder', async () => {
  const source = {repository:'https://github.com/someone/opxy-probe', tag:'v1.0.0', commit:'a'.repeat(40), maintainer:'someone'};
  assert.ok(validSource(source));
  assert.equal(sourceUrl(source), 'https://github.com/someone/opxy-probe/tree/' + 'a'.repeat(40));
  assert.equal(sourceName(source), 'someone/opxy-probe');
  for (const change of [{repository:'javascript:alert(1)'}, {repository:'https://example.com/a/b'},
                        {repository:'https://github.com/someone/..'}, {repository:'https://github.com/someone/.'},
                        {repository:['https://github.com/a/b']}, {commit:'abc'}, {tag:'v 1'}, {maintainer:'not a user'},
                        {extra:1}])
    assert.ok(!validSource({...source, ...change}), JSON.stringify(change));
  assert.ok(!validSource(null) && !validSource(undefined) && !validSource('https://github.com/a/b'));
  assert.ok(validOrigin({origin:'catalog', source}) && validOrigin({origin:'personal'}));
  // The SDK bundles no machines, so no machine comes from it.
  for (const m of [{origin:'catalog'}, {origin:'official'}, {origin:'bundled'}, {origin:'bundled', source}, {origin:'personal', source}, {},
                   {origin:'community'}])
    assert.ok(!validOrigin(m), JSON.stringify(m));
  assert.ok(validOrigin({origin:'official', source}));
  // Community machines start unselected; Open-XY's own and the user's start selected.
  assert.deepEqual(['official', 'catalog', 'personal'].map(origin => chosenByDefault({origin})), [true, false, true]);
  const k=fixture(); k.format='opxy-browser-kit/1'; k.profileSource=profileBytes.toString('base64');
  k.objects=[]; k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg',import.meta.url),'utf8');
  Object.assign(k.machines[0], {origin:'catalog', source}); k.fixed={}; await openKit(k);
  // A machine with fixed numbers (kit.fixed), from wherever it comes, uses exactly those.
  k.fixed={a:{engine:34,page:82}}; await openKit(k);
  k.fixed={a:{engine:35,page:83}}; await assert.rejects(openKit(k), /Fixed machine numbers differ: a/); k.fixed={};
  k.machines[0].source = {...source, repository:'https://evil.example/a/b'};
  await assert.rejects(openKit(k), /Invalid machine origin/);
  k.machines[0].source = source;
  // Catalog keywords are words from one list, at most five, each once.
  for (const keywords of [['fm', 'bell'], undefined]) { k.machines[0].keywords = keywords; await openKit(k); }
  for (const keywords of [[], ['fm', 'fm'], ['FM'], ['<b>'], 'fm', ['fm', 'bell', 'pad', 'bass', 'lofi', 'noise']]) {
    k.machines[0].keywords = keywords;
    await assert.rejects(openKit(k), /Invalid machine keywords/, JSON.stringify(keywords));
  }
});
test('a kit with a machine named after stock content is refused when it opens', async () => {
  const k=fixture(); k.format='opxy-browser-kit/1'; k.profileSource=profileBytes.toString('base64');
  k.objects=[]; k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg',import.meta.url),'utf8');
  k.fixed={}; await openKit(k);
  // A kit exported before the profile listed stock's names carries that older profile.
  const older = JSON.parse(profileBytes); delete older.stock_content;
  const olderSource = Buffer.from(JSON.stringify(older, null, 2) + '\n').toString('base64');
  await assert.rejects(openKit({...k, profile:older, profileSource:olderSource}),
    /not the reviewed profile: .*Export the kit again with this SDK/);
  for (const [id, stock] of [['monitor', 'content/display/monitor.svg'], ['pad', 'content/presets/pad'], ['snare', 'content/samples/snare']]) {
    k.machines[0] = machine(id, 34, 82);
    await assert.rejects(openKit(k), new RegExp(`Machine ${id}: its ID names the OP-XY’s own ${stock}\\. `), id);
  }
});
test('machines are found by name, author, kind, description and keyword, in a chosen order', () => {
  const by = (maintainer, name = 'x') => ({repository:'https://github.com/' + maintainer + '/' + name, tag:'v1', commit:'a'.repeat(40), maintainer});
  const machines = [
    {id:'zeta', name:'Zeta', kind:'synth', origin:'catalog', description:'Bright pads.', source:by('bob', 'OP-XY-Hexagon'), keywords:['pad', 'fm']},
    {id:'alpha', name:'alpha', kind:'effect', origin:'catalog', description:'A tape delay.', source:by('Carol'), keywords:['delay']},
    {id:'core', name:'Mid', kind:'synth', origin:'official', description:'Plucked strings.', source:by('etc'), keywords:['pluck']},
    {id:'mine', name:'Beta 10', kind:'drum', origin:'personal', description:'Kicks.'},
    {id:'beta2', name:'Beta 2', kind:'synth', origin:'catalog', description:'Granular clouds.', source:by('bob')},
  ];
  const ids = options => browseMachines(machines, options).map(({m}) => m.id);
  // Open-XY's own first, then the rest, each by name; each row keeps its collection index (its colour).
  assert.deepEqual(ids(), ['core', 'alpha', 'beta2', 'mine', 'zeta']);
  assert.deepEqual(browseMachines(machines).map(({index}) => index), [2, 1, 4, 3, 0]);
  assert.deepEqual(ids({sort:'name'}), ['alpha', 'beta2', 'mine', 'core', 'zeta']);
  // By maintainer, ignoring case; a personal machine has none and comes first.
  assert.deepEqual(ids({sort:'author'}), ['mine', 'beta2', 'zeta', 'alpha', 'core']);
  assert.deepEqual(ids({sort:'kind'}), ['beta2', 'core', 'zeta', 'mine', 'alpha']);
  assert.deepEqual(ids({sort:'unknown'}), ids());
  assert.deepEqual(ids({query:'BOB'}), ['beta2', 'zeta']);
  assert.deepEqual(ids({query:'  tape   delay '}), ['alpha']);
  assert.deepEqual(ids({query:'pad bob'}), ['zeta']);
  assert.deepEqual(ids({query:'drum'}), ['mine']);
  // A collection by its repository's name, and kinds by the words the builder shows for them.
  assert.deepEqual(ids({query:'hexagon'}), ['zeta']);
  assert.deepEqual(ids({query:'drums'}), ['mine']);
  assert.deepEqual(ids({query:'fx'}), ['alpha']);
  assert.deepEqual(ids({query:'synthesizer'}), ['core', 'beta2', 'zeta']);
  assert.deepEqual(ids({query:'nothing'}), []);
  assert.deepEqual(ids({kind:'synth'}), ['core', 'beta2', 'zeta']);
  assert.deepEqual(ids({keywords:['pad']}), ['zeta']);
  assert.deepEqual(ids({keywords:['pad', 'delay']}), []);
  assert.deepEqual(ids({favorites:new Set(['alpha', 'mine', 'gone'])}), ['alpha', 'mine']);
  assert.deepEqual(ids({kind:'synth', query:'b', sort:'name'}), ['beta2', 'zeta']);
  assert.equal(machineAuthor(machines[3]), '');
  assert.equal(machineAuthor(machines[1]), 'Carol');
  assert.ok(MACHINE_KEYWORDS.length && MACHINE_KEYWORDS.every(keyword => /^[a-z]+$/.test(keyword)));
  // Most used first, then by name: the order keyword filters are kept when only some fit.
  assert.deepEqual(keywordsByUse([...machines, {id:'pads', keywords:['pad', 'bass']}]),
    [{keyword:'pad', count:2}, {keyword:'bass', count:1}, {keyword:'delay', count:1}, {keyword:'fm', count:1}, {keyword:'pluck', count:1}]);
  assert.deepEqual(keywordsByUse([{id:'none'}]), []);
});
test('the CPU-high label follows the reported peak, or the average when no peak is reported', () => {
  assert.equal(CPU_HIGH_PERCENT, 4.5);
  // The peak decides when there is one, even under a heavier average.
  assert.deepEqual(cpuWorstCase({cpu:{block_percent:4.8, peak_percent:4.4}}), {percent:4.4, metric:'peak'});
  assert.equal(cpuHigh({cpu:{block_percent:4.8, peak_percent:4.4}}), null);
  const peak = cpuHigh({cpu:{block_percent:3.69, peak_percent:6.314183}});
  assert.equal(peak.metric, 'peak');
  assert.equal(peak.detail, 'worst observed block peak 6.31% of an audio block, above 4.5%');
  assert.equal(peak.text, 'CPU-high: ' + peak.detail);
  // Without a peak, the average, and the text says so.
  const average = cpuHigh({cpu:{block_percent:8.54963}});
  assert.equal(average.metric, 'average');
  assert.equal(average.text, 'CPU-high: average 8.55% of an audio block, above 4.5% (no peak reported)');
  // A drum kit, by its worst single sound: its peak includes whole-kit runs and is ignored,
  // so a heavy kit of light sounds is not labelled; its single-voice peak decides, or
  // without one its heaviest voice's average.
  const kit = {kind:'drum', cpu:{block_percent:2.7, peak_percent:48.163577, kit_percent:23.7, ceiling_percent:41.9}};
  assert.deepEqual(cpuWorstCase(kit), {percent:2.7, metric:'average'});
  assert.equal(cpuHigh(kit), null);
  assert.equal(cpuHigh({...kit, cpu:{...kit.cpu, voice_peak_percent:4.2}}), null);
  const sound = cpuHigh({...kit, cpu:{...kit.cpu, voice_peak_percent:7.25}});
  assert.equal(sound.metric, 'peak');
  assert.equal(sound.text, 'CPU-high: worst single sound peak 7.25% of an audio block, above 4.5%');
  // A synth's record has no single-voice peak field; a stray one is not read.
  assert.equal(cpuHigh({kind:'synth', cpu:{block_percent:2, peak_percent:3, voice_peak_percent:9}}), null);
  for (const cpu of [{block_percent:5, peak_percent:48}, {block_percent:5}])
    assert.equal(cpuHigh({kind:'drum', cpu}).text,
      'CPU-high: worst single sound average 5% of an audio block, above 4.5% (no single-sound peak recorded)');
  // A synth with a track stage: one voice and its track's stage (tooling.cpu_measurement.track_cost).
  const staged = {kind:'synth', cpu:{block_percent:2, peak_percent:2.5, stage_percent:1, stage_peak_percent:2.1}};
  assert.deepEqual(cpuWorstCase(staged), {percent:4.6, metric:'peak', stage:true});
  assert.equal(cpuHigh(staged).text, 'CPU-high: worst observed block peak 4.6% of an audio block, above 4.5% (a voice with its track stage)');
  assert.equal(cpuHigh({kind:'synth', cpu:{...staged.cpu, stage_peak_percent:2}}), null);
  assert.deepEqual(cpuWorstCase({kind:'synth', cpu:{block_percent:2, stage_percent:2.6, stage_peak_percent:3}}),
    {percent:4.6, metric:'average', stage:true});
  // Only a synth's record carries them; a kit's stray fields are not read.
  assert.deepEqual(cpuWorstCase({kind:'drum', cpu:{block_percent:2, stage_percent:3, stage_peak_percent:4}}),
    {percent:2, metric:'average'});
  // Strictly above the threshold; no record, or no usable figure, is no label.
  assert.equal(cpuHigh({cpu:{block_percent:4.5}}), null);
  assert.equal(cpuHigh({cpu:{block_percent:3, peak_percent:4.5}}), null);
  for (const m of [{}, {cpu:null}, {cpu:'5'}, {cpu:{block_percent:'9'}}, {cpu:{block_percent:NaN}}]) assert.equal(cpuHigh(m), null, JSON.stringify(m));
  assert.deepEqual(cpuWorstCase({cpu:{block_percent:5, peak_percent:'x'}}), {percent:5, metric:'average'});
  // Derived from every catalog entry's record, so it changes only when a report does.
  const catalog = JSON.parse(readFileSync(new URL('../catalog/machines.json', import.meta.url), 'utf8')).machines;
  for (const entry of catalog.filter(e => e.cpu)) {
    // A kit's worst single sound: its single-voice peak, else its heaviest voice's average;
    // anything else, its peak or else its average.
    const worst = entry.kind === 'drum' ? entry.cpu.voice_peak_percent ?? entry.cpu.block_percent
      : entry.cpu.peak_percent ?? entry.cpu.block_percent;
    assert.equal(Boolean(cpuHigh(entry)), worst > CPU_HIGH_PERCENT, entry.id);
  }
});
test('machine presets save, replace, delete and choose named selections by ID', () => {
  let presets = savePreset([], '  Night   set ', ['moth', 'fm2op']);
  assert.deepEqual(presets, [{name:'Night set', machines:['moth', 'fm2op']}]);
  presets = savePreset(presets, 'ambient 2', ['bloom']);
  presets = savePreset(presets, 'Ambient 10', ['wane']);
  // By name, numbers by value; saving a name again in any case replaces it in place.
  assert.deepEqual(presets.map(p => p.name), ['ambient 2', 'Ambient 10', 'Night set']);
  presets = savePreset(presets, 'NIGHT SET', ['karplus', 'karplus']);
  assert.deepEqual(presets.map(p => [p.name, p.machines]), [['ambient 2', ['bloom']], ['Ambient 10', ['wane']], ['NIGHT SET', ['karplus']]]);
  for (const [name, machines, message] of [['', ['moth'], /Name the preset/], ['   ', ['moth'], /Name the preset/],
    ['x'.repeat(PRESET_NAME_LIMIT + 1), ['moth'], /40 characters/], ['empty', [], /at least one machine/]])
    assert.throws(() => savePreset(presets, name, machines), message, name);
  let many = [];
  for (let i = 0; i < 50; i++) many = savePreset(many, 'p' + i, ['moth']);
  assert.throws(() => savePreset(many, 'one more', ['moth']), /at most 50/);
  savePreset(many, 'P7', ['bloom']);  // replacing one is still allowed
  assert.deepEqual(deletePreset(presets, 'night set').map(p => p.name), ['ambient 2', 'Ambient 10']);
  // Update saves the selection under the preset's own name; a matching selection, in any order, changes nothing.
  const night = {name:'Night', machines:['moth', 'fm2op']};
  assert.ok(presetMatches(night, ['fm2op', 'moth']) && presetMatches(night, ['moth', 'fm2op', 'moth']));
  for (const machines of [['moth'], ['moth', 'fm2op', 'bloom'], ['moth', 'bloom'], []])
    assert.ok(!presetMatches(night, machines), machines.join());
  assert.deepEqual(savePreset([night], night.name, ['bloom']), [{name:'Night', machines:['bloom']}]);
  // Reading what a browser kept drops what is malformed, repeated or too long.
  assert.deepEqual(readPresets(null), []);
  assert.deepEqual(readPresets('[]'), []);
  assert.deepEqual(readPresets([{name:'b', machines:['moth']}, {name:'B', machines:['bloom']}, {name:'a', machines:['moth']},
    {name:'', machines:['moth']}, {name:'c', machines:[]}, {name:'d', machines:['Moth']}, {name:'e', machines:['moth', 'moth']},
    {name:'f', machines:'moth'}, {name:'x'.repeat(41), machines:['moth']}, {name:3, machines:['moth']}, null, 'g',
    {name:'h', machines:[['moth']]}, {name:'i', machines:['a'.repeat(16)]}]),
    [{name:'a', machines:['moth']}, {name:'b', machines:['moth']}]);
  assert.equal(readPresets(Array.from({length:60}, (_, i) => ({name:'p' + i, machines:['moth']}))).length, 50);
  // Choosing one selects the machines this kit has, in collection order, and names the rest.
  const kit = {machines:[{id:'fm2op'}, {id:'karplus'}, {id:'moth'}]};
  assert.deepEqual(applyPreset(kit, {name:'n', machines:['moth', 'gone', 'fm2op']}), {selected:['fm2op', 'moth'], missing:['gone']});
  assert.deepEqual(applyPreset(kit, {name:'n', machines:['gone']}), {selected:[], missing:['gone']});
});
test('malformed loader records and unsafe ZIP paths are rejected', () => {
  for(const bytes of [new Uint8Array(),new Uint8Array(16),elf()]) assert.throws(()=>readLdr(bytes));
  for(const path of ['../os.ldr','/os.ldr','x/../../y','x\\y']) assert.throws(()=>zipFiles({[path]:new Uint8Array(4)}),/bundle path/);
  const zip=zipFiles({'layout.json':Buffer.from('{}')}); assert.equal(new DataView(zip.buffer).getUint32(0,true),0x04034b50);
});

test('firmware recovery rejects malformed and foreign loader layouts without private inputs', async () => {
  const kit = fixture();
  for (const bytes of [new Uint8Array(), new Uint8Array(16), elf()])
    await assert.rejects(recoverFirmware(kit, bytes));
  // Authored valid FINAL record, not a stock or Open-XY container.
  const final = new Uint8Array(16);
  new DataView(final.buffer).setUint32(0, 0xad008004, true);
  final[2] = final.reduce((a,b) => a ^ b, 0);
  await assert.rejects(recoverFirmware(kit, final), /Only OP–XY OS 1\.2\.1 is supported/);
  kit.machines = Array.from({length:11},(_,i) => machine('m'+i,34+i,82+i));
  await assert.rejects(recoverFirmware(kit, final), /Only OP–XY OS 1\.2\.1 is supported/);
});

test('the release checks the linked machines, each other machine beside the official ones, and their subsets', () => {
  const machines = [machine('a',34,82,'official'), machine('b',35,82,'official'), machine('c',36,82), machine('d',37,82)];
  // Everything fits: the native package links all four.
  assert.deepEqual(checkedSelections(machines), [1n, 2n, 3n, 7n, 11n, 15n]);
  // Not all at once: the native package links the official machines.
  assert.deepEqual(checkedSelections(machines, ['a', 'b']), [1n, 2n, 3n, 7n, 11n]);
  // More machines than a 32-bit mask holds, with official ones past bit 31.
  const many = Array.from({length: 40}, (_, i) => machine('m' + i, 34, 82, i === 33 || i === 39 ? 'official' : undefined));
  const core = (1n << 33n) | (1n << 39n);
  const selections = checkedSelections(many, ['m33', 'm39']);
  assert.deepEqual(selections.slice(0, 3), [1n << 33n, 1n << 39n, core]);
  assert.equal(selections.length, 3 + 38);
  assert.ok(selections.slice(3).every(mask => (mask & core) === core));
});


test('players have stable separate types and share the relocated page pool', () => {
  const profile = JSON.parse(readFileSync(new URL('../profiles/opxy-1.2.1.json', import.meta.url), 'utf8'));
  const rules = profile.identities;
  const machines = [['probeplayer', 'player', {player: null, page: null}], ['second', 'player', {player: null, page: null}]];
  const layout = numberLayout(rules, [], null, machines);
  assert.notEqual(layout.probeplayer.player, layout.second.player);
  assert.notEqual(layout.probeplayer.page, layout.second.page);
  assert.deepEqual(numberLayout(rules, [], layout, machines.slice().reverse()), layout);
  assert.deepEqual([...activeServices(profile, [{kind: 'player'}])].sort(), ['octaves', 'players']);
  assert.throws(() => numberLayout(rules, [], {probeplayer: {player: 4, page: 84}}, machines), /cannot use/);
});


test('saved selections retain empty choices and available entries after catalog changes', () => {
  const kit = {profile:{id:'test'}, machines:[{id:'a'}, {id:'b'}, {id:'new'}],
    extensions:[{id:'observer',claims:[]}, {id:'capture',claims:['record_double_tap']}]};
  const saved = {format:'opxy-selection/1',profile:'test',selected:['b','missing','a','a'],extensions:['missing','capture']};
  assert.deepEqual(readSelection(kit,saved), {selected:['a','b'],extensions:['capture']});
  assert.deepEqual(readSelection(kit,{...saved,selected:[],extensions:[]}), {selected:[],extensions:[]});
  for (const invalid of [null, {...saved,profile:'other'}, {...saved,selected:'a'}, {...saved,extensions:[null]}])
    assert.throws(() => readSelection(kit,invalid), /Invalid saved selection/);
});

test('new extension conflicts preserve saved machines and remain visible until resolved', () => {
  const kit = {profile:{id:'test'}, machines:[{id:'kept'}], extensions:[
    {id:'first',claims:['record_double_tap']}, {id:'second',claims:['record_double_tap']},
    {id:'third',claims:['record_double_tap']}]};
  const saved = {format:'opxy-selection/1',profile:'test',selected:['kept'],extensions:['third','second','first']};
  const restored = readSelection(kit,saved);
  assert.deepEqual(restored, {selected:['kept'],extensions:['first','second','third']});
  assert.throws(() => extensionSelection(kit,restored.extensions), /record double tap/);
  assert.deepEqual(readSelection(kit,{...saved,...restored}),restored);
  assert.deepEqual(extensionSelection(kit,['first']),['first']);
});

test("a kit's keys, models and header read as the device's adapter reads them", () => {
  assert.deepEqual([KIT_KEYS[0], KIT_KEYS.at(-1), KIT_KEYS.length], [53, 76, 24]);
  // Stored 0..127, played as Q31: 127 is full scale.
  assert.deepEqual([0, 1, 64, 126, 127].map(kitWord), [0, 0x1000000, 0x40000000, 0x7e000000, 0x7fffffff]);
  const kit = {models:33, padMix:true, groups:[{label:'TRX', count:13}, {label:'EFM', count:8}, {label:'P-I', count:9}, {label:'GND', count:3}]};
  assert.deepEqual([0, 12, 13, 20, 21, 32].map(model => kitChoices(kit, model, 2)),
    [[0, 0, 2, 0], [0, 12, 2, 0], [1, 0, 2, 0], [1, 7, 2, 0], [2, 0, 2, 0], [3, 2, 2, 0]]);
  assert.deepEqual(kitGroup(kit, 30), {group:3, place:0, label:'GND'});
  assert.deepEqual(kitChoices({models:12}, 7), [7, 0, 0, 0]);
  // The drum-kit controls (SDK 1.23): a key not set up reads 0 dB (30) and no random; with
  // groups the volume takes the choke group's place.
  assert.deepEqual(kitChoices({models:12, padVolume:true, padRandom:true}, 7, 2), [7, 2, 30, 0]);
  assert.deepEqual(kitChoices({models:5, padVolume:true, groups:[{label:'A', count:2}, {label:'B', count:3}]}, 3, 1),
    [1, 1, 30, 0]);
  assert.deepEqual([kitSelectedKey({models:2, selectedKey:true}, 76), kitSelectedKey({models:2}, 76)], [23, 0]);
  assert.equal(kitGroup({models:12}, 7).label, '');
  assert.deepEqual(kitMix(kit), {pan:0, level:0, pad:[0, 0, 0, 0]});
  assert.deepEqual(kitMix({models:12}), {pan:0, level:0});
  const memory = {buffer:new ArrayBuffer(256)}, bytes = new Uint8Array(memory.buffer);
  bytes.set(new TextEncoder().encode('BD\0PTCH\0'), 100);
  const api = {memory, opxy_drum_defaults:key => { bytes.set([key - 53, 200, 127, 0, 1, 2, 3, 4, 5], 8); return 8; },
    opxy_drum_model_name:() => 100, opxy_drum_label:(model, slot) => slot === 0 ? 103 : slot === 1 ? 0 : 102, opxy_drum_count:() => 9};
  assert.deepEqual(kitKey(api, kit, 55), {key:55, model:2, controls:[127, 127, 0, 1, 2, 3, 4, 5], choke:0});
  // A model beyond the kit reads as its first.
  assert.equal(kitKey(api, kit, 76).model, 23);
  assert.equal(kitKey(api, {models:20}, 76).model, 0);
  assert.throws(() => kitKey(api, kit, 52), /Invalid kit key/);
  // Eight slots at most; a null or empty label is an unused slot.
  const names = kitModel(api, kit, 0);
  assert.deepEqual(names, {name:'BD', labels:['PTCH', '', '', '', '', '', '', ''], used:[true, false, false, false, false, false, false, false]});
  assert.throws(() => kitModel(api, kit, 33), /Invalid kit model/);
  assert.deepEqual(kitHeader(names, [9, 8, 7, 6, 5, 4, 3, 2], [0, 1, 6, 3]), [['PTCH', '', '', ''], ['9', '', '', '']]);
  // Unterminated or out-of-range text fails closed.
  bytes.fill(65, 200);
  assert.throws(() => kitModel({...api, opxy_drum_model_name:() => 200}, kit, 0), /Unterminated kit text/);
  assert.throws(() => kitModel({...api, opxy_drum_model_name:() => 4096}, kit, 0), /Invalid kit text/);
});
