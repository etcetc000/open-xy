import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {activeServices, readObject, identities, linkKit, moduleBytes, openKit, readLdr, recoverFirmware, sourceUrl, validSource, zipFiles, sha256} from '../builder/core.mjs';

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
function machine(id, engine, page) { return {id, kind:'synth', identity:{engine,page}, absent:{}}; }
const profileBytes = readFileSync(new URL('../profiles/opxy-1.2.0.json',import.meta.url));
function fixture() {
  const object = {...readObject(elf()), name:'a.o',owner:'a'};
  return {profile:JSON.parse(profileBytes),definitions:{},machines:[machine('a',34,82)],objects:[object]};
}
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
test('page and engine collisions and incompatible restored layouts fail', () => {
  const k=fixture(), id=k.profile.id;
  k.machines[0].identity.engine=k.profile.identities.engine_none; assert.throws(()=>identities(k),/stock slot/);
  k.machines[0].identity.engine=34;
  k.machines.push(machine('b',35,82)); assert.throws(()=>identities(k),/Conflicting page/); k.machines.pop();
  k.machines.push(machine('b',34,83)); assert.throws(()=>identities(k),/Conflicting engine/); k.machines.pop();
  const sample=page=>({id:'s'+page,kind:'sample',identity:{engine:40+page,page},absent:{}});
  k.machines.push(sample(7),sample(2),{...sample(7),id:'t',identity:{engine:50,page:7}}); identities(k); k.machines.length=1;
  assert.throws(()=>identities(k,{format:'opxy-kit/1',profile:'wrong',selected:[]}),/different OS version/);
  assert.throws(()=>identities(k,{format:'opxy-kit/1',profile:id,selected:['a'],identities:{a:{engine:35,page:82}}}),/changed/);
  assert.deepEqual(identities(k,{format:'opxy-kit/1',profile:id,selected:['a'],identities:{a:{page:82,engine:34}}}),{a:{engine:34,page:82}});
});
test('IDs do not change when selection order changes', () => {
  const k=fixture(); k.machines.push(machine('b',35,83)); const before=identities(k);
  linkKit(k,['b','a']); linkKit(k,['a']); assert.deepEqual(identities(k),before);
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
  k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.0/screen.svg',import.meta.url),'utf8');
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
  k.objects[0].sha256='0'.repeat(64); await assert.rejects(openKit(k),/hash mismatch/);
  k.profile.patches[0].after='0000000000000000'; await assert.rejects(openKit(k),/reviewed profile/);
});
test('a catalog machine names its GitHub repository, release and commit; bundled machines name none', async () => {
  const source = {repository:'https://github.com/someone/opxy-probe', tag:'v1.0.0', commit:'a'.repeat(40), maintainer:'someone'};
  assert.ok(validSource(source) && validSource(undefined));
  assert.equal(sourceUrl(source), 'https://github.com/someone/opxy-probe/tree/' + 'a'.repeat(40));
  for (const change of [{repository:'javascript:alert(1)'}, {repository:'https://example.com/a/b'}, {commit:'abc'},
                        {tag:'v 1'}, {maintainer:'not a user'}, {extra:1}])
    assert.ok(!validSource({...source, ...change}), JSON.stringify(change));
  assert.ok(!validSource(null) && !validSource('https://github.com/a/b'));
  const k=fixture(); k.format='opxy-browser-kit/1'; k.profileSource=profileBytes.toString('base64');
  k.objects=[]; k.stockCatalog=''; k.screenTemplate=readFileSync(new URL('../sdk/platform/opxy-1.2.0/screen.svg',import.meta.url),'utf8');
  k.machines[0].source = source; await openKit(k);
  k.machines[0].source = {...source, repository:'https://evil.example/a/b'};
  await assert.rejects(openKit(k), /Invalid machine source/);
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
  await assert.rejects(recoverFirmware(kit, final), /Only OP–XY OS 1\.2\.0 is supported/);
  kit.machines = Array.from({length:11},(_,i) => machine('m'+i,34+i,82+i));
  await assert.rejects(recoverFirmware(kit, final), /can.t be recognized/);
});
