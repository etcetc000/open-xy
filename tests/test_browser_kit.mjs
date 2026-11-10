import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {MACHINE_KEYWORDS, keywordsByUse, browseMachines, machineAuthor, CPU_HIGH_PERCENT, cpuWorstCase, cpuHigh, PRESET_NAME_LIMIT, readPresets, savePreset, deletePreset, presetMatches, applyPreset, activeServices, assignNumbers, checkedSelections, chosenByDefault, claimLabel, extensionChoices, extensionSelection, sharedClaim, numberLayout, readLayout, readObject, linkKit, moduleBytes, expandedProfile, memoryKit, offersExpandedMemory, openKit, patchFirmware, presentSelections, readLdr, recoverFirmware, RECOVERABLE_ALTERNATIVES, sourceName, sourceUrl, validOrigin, validSource, zipFiles, sha256} from '../builder/core.mjs';

// Tiny authored ELF fixture, independent of private firmware/toolchains.
function elf() {
  const strings = Buffer.from('\0entry\0'), names = Buffer.from('\0.text\0.symtab\0.strtab\0.shstrtab\0');
  const table = 128, bytes = new Uint8Array(table + 5 * 40), d = new DataView(bytes.buffer);
  const w16 = (at, n) => d.setUint16(at, n, true), w32 = (at, n) => d.setUint32(at, n, true);
  bytes.set([127,69,76,70,1,1,1]); w16(16,1); w16(18,106); w32(20,1); w32(32,table);
  w16(40,52); w16(46,40); w16(48,5); w16(50,4);
  bytes.set([0,96,16,0],52); // R0=0; RTS.
  w32(72,1); w32(80,4); bytes[84] = 0x12; w16(86,1);
  bytes.set(strings,88); bytes.set(names,96);
  const section = (i, fields) => fields.forEach((v,j) => w32(table + i * 40 + j * 4,v));
  section(1,[1,1,6,0,52,4,0,0,4,0]);
  section(2,[7,2,0,0,56,32,3,1,4,16]);
  section(3,[15,3,0,0,88,strings.length,0,0,1,0]);
  section(4,[23,3,0,0,96,names.length,0,0,1,0]);
  return bytes;
}
function machine(id, engine, page, origin='personal') { return {id, prefix:id, kind:'synth', identity:{engine,page}, absent:{}, origin}; }
const FOUR_MIB = 4 * 1024 ** 2;
const profileBytes = readFileSync(new URL('../profiles/opxy-1.2.1.json',import.meta.url));
function fixture() {
  const object = {...readObject(elf()), name:'a.o',owner:'a'};
  return {version:'0.0.2',profile:JSON.parse(profileBytes),definitions:{},fixed:{a:{engine:34,page:82}},machines:[machine('a',34,82)],objects:[object]};
}

// An authored loader with two diagnostic hooks and one ordinary hook. No stock
// instructions or firmware bytes are needed to exercise full reconstruction.
async function profilingFixture() {
  const kit = fixture(), stock = new Uint8Array(96), d = new DataView(stock.buffer);
  const record = (at, flags, address, size, argument = 0) => {
    [0xad000000 | flags, address, size, argument].forEach((v, i) => d.setUint32(at + i * 4, v, true));
    stock[at + 2] = stock.subarray(at, at + 16).reduce((a, b) => a ^ b, 0);
  };
  record(0, 0x5004, 0x1000, 0, stock.length - 16);
  record(16, 4, 0x1000, 32);
  stock.set(Array.from({length:32}, (_, i) => i + 1), 32);
  record(64, 0x104, 0x2000, 256);
  record(80, 0x8004, 0, 0);
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

test('declared callbacks of unselected machines resolve to fallbacks only for SDK objects', () => {
  const k=fixture(), o=k.objects[0];
  k.definitions.fallback=k.profile.module_base;
  const missing={id:'missing',prefix:'probe',kind:'sample',identity:{engine:46,page:7},absent:{probe_ready:'fallback'}};
  k.machines.push(missing);
  o.symbols.push({name:'probe_ready',section:0,value:0,size:0,binding:1,type:0});
  o.relocations.push({section:1,offset:0,type:18,symbol:2,addend:0});
  o.owner=null; assert.doesNotThrow(()=>linkKit(k,['a']));
  o.owner='a'; assert.throws(()=>linkKit(k,['a']),/Unresolved import/);
  o.owner=null; o.symbols[2].name='probe_internal';
  assert.throws(()=>linkKit(k,['a']),/Unresolved import/);
  o.symbols[2].name='probe_ready'; missing.absent={probe_ready:'undefined_fallback'};
  assert.throws(()=>linkKit(k,['a']),/absent-machine callback/);
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
  const k=fixture(); k.machines.push({id:'g',prefix:'g',kind:'sample',identity:{engine:null,page:null},absent:{},origin:'personal'});
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
test('SDK 1.5 machines ask for no numbers and the selection gives them', async () => {
  const k=fixture(); k.format='opxy-browser-kit/1'; k.profileSource=profileBytes.toString('base64');
  k.objects=[{name:'a.o',owner:'a',bytes:Buffer.from(elf()).toString('base64'),sha256:await sha256(elf())}];
  k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.1/screen.svg',import.meta.url),'utf8');
  // a has fixed numbers yet asks for none; a sample engine names only its base's page.
  k.machines=[{...machine('a',null,null)},{...machine('b',null,null)},
    {id:'d',prefix:'d',kind:'sample',identity:{engine:null,page:7},absent:{},origin:'personal'},
    {id:'e',prefix:'e',kind:'effect',identity:{effect:null,page:21},absent:{},origin:'personal'}];
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
