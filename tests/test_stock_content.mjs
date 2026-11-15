// The browser builder refuses to install a file over the OP-XY's own content
// (builder/core.mjs checkStockId and checkStockContent, as tooling/stock_content.py does for sdk.py).
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {buildAssets, checkStockId, sha256, stockContentFile} from '../builder/core.mjs';

const read = path => readFileSync(new URL('../' + path, import.meta.url), 'utf8');
const profile = JSON.parse(read('profiles/opxy-1.2.1.json'));
// A file replaces a stock file, or goes into a stock folder; the message says which.
const refused = (stock, folder = false) =>
  new RegExp(`would install .* ${folder ? 'into' : 'over'} the OP-XY’s own ${folder ? 'folder ' : ''}${stock}\\. .*Rename it`);

// An authored menu and machines; the reviewed profile's stock content names.
async function fixture(...machines) {
  const text = '{"engine_types":[{"type":"stock"}\n],"fx_types":[],"seq_types":[]}';
  const catalog = new TextEncoder().encode(text);
  const screenTemplate = read('sdk/platform/opxy-1.2.1/screen.svg');
  const kit = {machines, screenTemplate, extensions:[], profile:{stock_catalog_sha256:await sha256(catalog),
    screen_template_sha256:await sha256(new TextEncoder().encode(screenTemplate)),
    stock_catalog_inserts:{engine_types:text.indexOf('\n]'), fx_types:text.indexOf('],"seq_types"'), seq_types:text.lastIndexOf(']')},
    stock_content:profile.stock_content}};
  return {kit, catalog};
}
const synth = (id, extra = {}) => ({id, kind:'synth', name:id,
  catalog:`,{"type":"${id}","default_params":[0,0,0,0,0,0,0,0]}`, ...extra});

test('a machine screen named after a stock screen is refused', async () => {
  const {kit, catalog} = await fixture(synth('tape', {artwork:'<g id="tape_panel"/>', asset:'tape.svg'}));
  await assert.rejects(buildAssets(kit, ['tape'], catalog, {}), refused('content/display/tape.svg'));
});

test('a machine preset folder named after a stock preset folder is refused', async () => {
  const {kit, catalog} = await fixture(synth('keys'));
  await assert.rejects(buildAssets(kit, ['keys'], catalog, {}), refused('content/presets/keys', true));
});

test('an extension artwork named after a stock screen is refused in any letter case', async () => {
  const {kit, catalog} = await fixture(synth('probe'));
  const bytes = new TextEncoder().encode('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222"/>');
  kit.extensions = [{id:'processor', master_processor:{asset:'Reverb.SVG',
    artwork:Buffer.from(bytes).toString('base64'), sha256:await sha256(bytes)}}];
  await assert.rejects(buildAssets(kit, ['probe'], catalog, {}, {}, ['processor']),
    /Extension processor would install content\/display\/Reverb\.SVG over the OP-XY’s own content\/display\/reverb\.svg/);
  kit.extensions[0].master_processor.asset = 'processor.svg';
  const files = await buildAssets(kit, ['probe'], catalog, {}, {}, ['processor']);
  assert.deepEqual(Object.keys(files).sort(), ['content/display/processor.svg', 'content/op1_factory.json',
    'content/presets/probe/default.json']);
});

test('names compare without regard to case, by content folder', () => {
  assert.equal(stockContentFile(profile, 'content/display/MONITOR.svg'), 'content/display/monitor.svg');
  assert.equal(stockContentFile(profile, 'content/presets/Keys/default.json'), 'content/presets/keys');
  assert.equal(stockContentFile(profile, 'content/samples/KICK'), 'content/samples/kick');
  for (const path of ['content/op1_factory.json', 'content/display', 'content/aux_presets/cv/init.json',
    'content/display/keys.svg', 'content/presets/tape'])
    assert.equal(stockContentFile(profile, path), null, path);
  assert.throws(() => stockContentFile({}, 'content/display/a.svg'), /lists no stock content names/);
});

test('every machine the catalog can build passes, and the only retired collision is known', () => {
  const entries = JSON.parse(read('catalog/machines.json')).machines;
  const colliding = [];
  for (const {id, status} of entries)
    try { checkStockId(profile, id); } catch { colliding.push(`${id} ${status}`); }
  assert.deepEqual(colliding, ['pluck retired']);
});
