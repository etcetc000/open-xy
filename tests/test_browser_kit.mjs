import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {activeServices, assignNumbers, chosenByDefault, numberLayout, readLayout, readObject, linkKit, moduleBytes, openKit, patchFirmware, readLdr, recoverFirmware, sourceName, sourceUrl, validOrigin, validSource, zipFiles, sha256} from '../builder/core.mjs';

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
  for (const image of [on, off]) assert.deepEqual(await recoverFirmware(kit, image), {stock, selected:['a']});
});

test('only an exact build restores its selection; others recover verified stock or are rejected', async () => {
  const {kit, linked, stock} = await profilingFixture();
  const on = await patchFirmware(kit, linked, stock), off = await patchFirmware(kit, linked, stock, {profiling:false});
  const earlier = {stock, selected:null, earlier:true};
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
  assert.deepEqual(await recoverFirmware(kit, exact), {stock, selected:null, earlier:true});
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
  assert.deepEqual([...activeServices(k.profile,[{kind:'synth'},{kind:'sample'}])],['samples']);
  // The service object redefines `entry`, so linking it would be a duplicate global.
  k.objects.push({...k.objects[0],name:'service.o',owner:null,service:'samples'});
  const linked=linkKit(k,['a']);
  assert.deepEqual(linked.objects.map(o=>o.name),['a.o']); assert.equal(linked.services.size,0);
  k.machines.push({...machine('b',46,7),kind:'sample'});
  assert.throws(()=>linkKit(k,['a','b']),/Duplicate global symbol: entry/);
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

test('sample drawing capability reaches kit identity validation and layout allocation', () => {
  const k=fixture();
  k.machines.push({...machine('orbit',34,82,'personal'),kind:'sample',asset:'orbit.svg'});
  const numbers=assignNumbers(k,['a','orbit']);
  assert.deepEqual(numbers.orbit,{engine:35,page:83});
  assert.deepEqual(assignNumbers(k,['orbit'],numbers).orbit,numbers.orbit);
  // Full linking also validates the requested identity using the display capability.
  assert.equal(linkKit(k,['a','orbit']).definitions.orbit_page_id,83);
  delete k.machines[1].asset;
  assert.throws(()=>linkKit(k,['a','orbit']),/no valid numbers/);
});
test('numbers follow the layout, not the selection order, and unselected machines link as 0', () => {
  const k=fixture(); k.machines.push(machine('b',34,82,'personal'),machine('c',34,82,'personal'));
  assert.deepEqual(assignNumbers(k,['c','b','a']),assignNumbers(k,['a','b','c']));
  assert.deepEqual(assignNumbers(k,['a','c']).c,{engine:35,page:83});
  // Once b has numbers, they stay held when b is dropped, and c takes the next free ones.
  const numbers=assignNumbers(k,['a','b']);
  assert.deepEqual(assignNumbers(k,['a','c'],numbers),{...numbers,c:{engine:36,page:84}});
  const linked=linkKit(k,['a','c'],numbers);
  assert.deepEqual([linked.definitions.a_engine_id,linked.definitions.b_engine_id,linked.definitions.c_engine_id,
    linked.definitions.c_page_id],[34,0,36,84]);
  assert.deepEqual(linked.numbers.b,{engine:35,page:83});
});
test('saved layouts restore numbers and selections, including older saved selections', () => {
  const k=fixture(), id=k.profile.id;
  assert.deepEqual(readLayout(k,{format:'opxy-layout/1',profile:id,selected:['a'],machines:{a:{engine:34,page:82},gone:{engine:40,page:90}}}),
    {selected:['a'],machines:{a:{engine:34,page:82},gone:{engine:40,page:90}}});
  assert.deepEqual(readLayout(k,{format:'opxy-kit/1',profile:id,selected:['a'],identities:{a:{page:82,engine:34}}}).machines,{a:{page:82,engine:34}});
  assert.throws(()=>readLayout(k,{format:'opxy-layout/1',profile:'wrong',machines:{}}),/different OS version/);
  assert.deepEqual(readLayout(k,{format:'opxy-layout/1',profile:'opxy-1.2.0',machines:{a:{engine:34,page:82}}}).machines,{a:{engine:34,page:82}});
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
