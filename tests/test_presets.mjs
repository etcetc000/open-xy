import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {buildAssets, presetFiles, presetPath, sha256} from '../builder/core.mjs';

const read = path => readFileSync(new URL('../' + path, import.meta.url), 'utf8');
// Catalog entries as the Python SDK writes them (any valid Q15 defaults).
const entry = (id, params) => ',\n        ' + JSON.stringify({type:id, default_params:params});
// Test fixture machines (tests/fixtures/machines): two synths and a sample engine with amp defaults.
const machines = ['probesynth', 'probesources', 'probesample'].map((id, i) => {
  const machine = JSON.parse(read(`tests/fixtures/machines/${id}/machine.json`));
  return {id, kind: machine.kind, catalog: entry(id, [i, 1000 * i, 32767, 0, 0, 0, 0, 0]),
    presetDefaults: machine.presetDefaults ?? {}};
});
machines.push({id:'probefx', kind:'effect',
  catalog:entry('probefx', [0, 32767, 0, 0, 0, 0, 0, 0]).replace('"type":"probefx"', '"type":"probefx","id":21')});
// The numbers a layout gave: the effect's menu entry carries its own.
const numbers = {probefx: {effect: 6, page: 21}};

async function fixture(selectedMachines = machines) {
  // Authored menu fixture with its own insertion points; no captured firmware or factory content.
  const text = '{"engine_types":[{"type":"existing"}\n    ],"fx_types":[{"type":"existing-fx"}\n    ],"seq_types":[{"type":"maestro"}\n    ]}';
  const catalog = new TextEncoder().encode(text);
  const stock_catalog_inserts = {engine_types: text.indexOf('\n    ]'), fx_types: text.indexOf('\n    ]', text.indexOf('fx_types')), seq_types: text.lastIndexOf('\n    ]')};
  return {kit: {machines:selectedMachines, profile:{stock_catalog_sha256:await sha256(catalog), stock_catalog_inserts}}, catalog};
}

test('all selections include only their instrument presets with native names and definition defaults', async () => {
  const {kit,catalog} = await fixture(), original = structuredClone(machines);
  for (let mask=1; mask<16; mask++) {
    const selected=machines.filter((_,i)=>mask & 2**i);
    const files=await buildAssets(kit,selected.map(m=>m.id),catalog,numbers);
    assert.deepEqual(Object.keys(files).sort(),['content/op1_factory.json',
      ...selected.filter(m=>m.kind!=='effect').map(m=>`content/presets/${m.id}/default.json`)].sort());
    for(const m of selected.filter(m=>m.kind!=='effect')) {
      const p=JSON.parse(new TextDecoder().decode(files[presetPath(m)]));
      assert.equal(p.type,{probesynth:'probesynth',probesources:'probesources',probesample:'probesample'}[m.id]);
      assert.equal(p.version,4); assert.equal(p.platform,'OP-XY');
      assert.deepEqual(p.engine.params,JSON.parse(m.catalog.slice(1)).default_params);
      assert.equal(p.engine.playmode,'poly');
      assert.deepEqual(p.regions,[]); // A preset never points into a user's sample library.
      assert.equal(p.fx.active,false); assert.equal(p.lfo.active,false);
      assert.ok(p.engine.volume>0 && p.engine.volume<=32767);
      assert.ok(p.envelope.amp.sustain>0);
      assert.equal(p.envelope.amp.release,32767); // Device-saved minimum release.
      for (const [key, value] of Object.entries(m.presetDefaults.amp ?? {})) assert.equal(p.envelope.amp[key], value);
      assert.equal(p.mtime,undefined);
    }
    const menu=JSON.parse(new TextDecoder().decode(files['content/op1_factory.json']));
    assert.deepEqual(menu.engine_types[0],{type:'existing'});
    if (selected.some(m=>m.id==='probefx')) assert.equal(menu.fx_types.at(-1).id,6);
    assert.deepEqual(Object.keys(files).filter(p=>p.startsWith('user/')),[]);
  }
  assert.deepEqual(machines,original);
});

// A machine's own presets as tooling.machines.read_presets passes them on.
const own = (changes = {}) => ({engine: {params: [1, 2, 3, 4, 5, 6, 7, 8], volume: 9000}, platform: 'OP-XY',
  regions: [], type: 'probesynth', version: 4, ...changes});

test("a machine's own presets install beside its init preset, unchanged", async () => {
  const synth = {...machines[0], presets: {'warm-pad': own(), bass_2: own({octave: -1})}};
  const {kit, catalog} = await fixture([synth, ...machines.slice(1)]);
  const files = await buildAssets(kit, ['probesynth', 'probefx'], catalog, numbers);
  assert.deepEqual(Object.keys(files).filter(p => p.startsWith('content/presets/')).sort(),
    ['content/presets/probesynth/bass_2.json', 'content/presets/probesynth/default.json',
      'content/presets/probesynth/warm-pad.json']);
  const read = path => new TextDecoder().decode(files[path]);
  assert.deepEqual(JSON.parse(read('content/presets/probesynth/bass_2.json')), own({octave: -1}));
  assert.equal(read('content/presets/probesynth/warm-pad.json'), JSON.stringify(own(), null, 2) + '\n');
  // The install guide lists the same files.
  assert.deepEqual(Object.keys(presetFiles(synth)).sort(), ['content/presets/probesynth/bass_2.json',
    'content/presets/probesynth/default.json', 'content/presets/probesynth/warm-pad.json']);
  assert.deepEqual(presetFiles(machines[3]), {});
  // Its own default replaces the generated init preset.
  const authored = presetFiles({...machines[0], presets: {default: own({octave: 1})}});
  assert.deepEqual(authored, {'content/presets/probesynth/default.json': own({octave: 1})});
});

test("a padMix kit's init preset keeps its lock words neutral", () => {
  // fx.params 0..3 are words 16..19, a padMix kit's step offsets (drum_locks.hpp): 0 would be the
  // largest cut, 16384 is none. Everything else is the shared template's.
  const kit = {...machines[0], id: 'probekit', kind: 'drum', catalog: entry('probekit', [0, 0, 0, 0, 0, 0, 0, 0]),
    features: ['drum_sends', 'hidePages', 'padMix', 'padSend']};
  const preset = presetFiles(kit)['content/presets/probekit/default.json'];
  const template = JSON.parse(read('tooling/default_preset.json'));
  assert.deepEqual(preset.fx, {...template.fx, params: [16384, 16384, 16384, 16384, ...template.fx.params.slice(4)]});
  const plain = presetFiles({...kit, features: ['padSend']})['content/presets/probekit/default.json'];
  assert.deepEqual(plain.fx, template.fx);
});

// Placeholder pending #234, which defines trackControl's real shape.
test('an aux track-control machine keeps no instrument preset folder', () => {
  // An aux machine has no catalog entry (tooling.browser_kit); its presets are aux presets.
  const aux = {...machines[0], catalog: ''};
  assert.equal(presetPath(aux), null);
  assert.deepEqual(presetFiles(aux), {});
});

test("a machine's own presets are checked again before they reach a device", async () => {
  for (const presets of [{default: own({type: 'x'})}, {'../x': own()}, {'Warm Pad': own()}, {a: own({type: 'fm2op'})},
    {a: own({version: 3})}, {a: own({platform: 'OP-1'})}, {a: own({regions: [{sample: 'kick.wav'}]})},
    {a: own({engine: {params: [0]}})}, {a: own({engine: {params: Array(8).fill(32768)}})}, {a: null},
    Object.fromEntries(Array.from({length: 17}, (_, i) => ['p' + i, own()])), []]) {
    const {kit, catalog} = await fixture([{...machines[0], presets}]);
    await assert.rejects(buildAssets(kit, ['probesynth'], catalog, numbers), /Invalid machine preset/);
  }
});

test('preset generation rejects unsafe paths, foreign kinds and invalid engine parameter values', async () => {
  for(const id of ['../probesynth','two/op','two\\op','']) assert.throws(()=>presetPath({id,kind:'synth'}));
  assert.throws(()=>presetPath({id:'example',kind:'unknown'}));
  for(const params of [[0],Array(8).fill(-1),Array(8).fill(32768),Array(8).fill(.5),Array(8).fill(null)]) {
    const m={id:'example',kind:'synth',catalog:', '+JSON.stringify({type:'example',default_params:params})};
    const {kit,catalog}=await fixture([m]);
    await assert.rejects(buildAssets(kit,['example'],catalog,numbers),/preset engine or defaults/);
  }
  const {kit,catalog}=await fixture([{id:'example',kind:'synth',catalog:',{"type":"../bad","default_params":[0,0,0,0,0,0,0,0]}'}]);
  await assert.rejects(buildAssets(kit,['example'],catalog,numbers),/preset engine or defaults/);
});

test("an aux machine's presets install as files, with the init of the role it replaces", () => {
  const preset = type => JSON.stringify({params: Array(8).fill(0), platform: 'OP-XY', type, version: 1});
  const auxPresets = {'content/aux_presets/cv/init.json': JSON.stringify({platform: 'OP-XY', type: 'cv', version: 1}),
    'content/aux_presets/morph/soft.json': preset('morph')};
  assert.deepEqual(Object.keys(presetFiles({id: 'morph', catalog: '', auxPresets})).sort(), Object.keys(auxPresets).sort());
  for (const [path, type] of [['content/aux_presets/cv/init.json', 'morph'], ['content/aux_presets/morph/a.json', 'cv'],
    ['content/aux_presets/brain/init.json', 'brain'], ['content/aux_presets/other/a.json', 'other'],
    ['content/aux_presets/morph/../a.json', 'morph']])
    assert.throws(() => presetFiles({id: 'morph', catalog: '', auxPresets: {[path]: preset(type)}}), /Invalid aux preset/);
});

test('a build refuses more aux presets than the OP-XY lists', async () => {
  const preset = type => JSON.stringify({params: Array(8).fill(0), platform: 'OP-XY', type, version: 1});
  const auxPresets = n => Object.fromEntries([['content/aux_presets/cv/init.json',
    JSON.stringify({platform: 'OP-XY', type: 'cv', version: 1})],
    ...Array.from({length: n}, (_, i) => [`content/aux_presets/probesynth/p${i}.json`, preset('probesynth')])]);
  const ok = await fixture([{...machines[0], catalog: '', auxPresets: auxPresets(63)}]);
  await buildAssets(ok.kit, ['probesynth'], ok.catalog, numbers);
  const over = await fixture([{...machines[0], catalog: '', auxPresets: auxPresets(64)}]);
  await assert.rejects(buildAssets(over.kit, ['probesynth'], over.catalog, numbers), /OP-XY lists 64/);
});
