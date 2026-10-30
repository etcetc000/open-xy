// Private input check. Emits only into the maintainer's ignored output directory.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
import {openKit, linkKit, patchFirmware, recoverFirmware, readLdr, buildAssets, zipFiles, checkedSelections} from '../builder/core.mjs';
const [directory, stockFile, content] = process.argv.slice(2);
const kit = await openKit(JSON.parse(readFileSync(resolve(directory, 'kit.json'))));
const stock = new Uint8Array(readFileSync(stockFile));
const catalog = new Uint8Array(readFileSync(resolve(content, 'op1_factory.json')));
assert.deepEqual(await recoverFirmware(kit, stock), {stock, selected:null});
const selections = JSON.parse(readFileSync(resolve(directory, 'reference/selections.json')));
for (const {name, selected, extensions} of selections) {
  const linked = linkKit(kit, selected, null, extensions), target = resolve(directory, 'reference', name);
  const firmware = await patchFirmware(kit, linked, stock);
  const recovered = await recoverFirmware(kit, firmware);
  assert.deepEqual(recovered, {stock, selected, extensions});
  assert.deepEqual(await patchFirmware(kit, linkKit(kit, recovered.selected, null, recovered.extensions), recovered.stock), firmware);
  const profilingOff = await patchFirmware(kit, linked, stock, {profiling: false});
  assert.notDeepEqual(profilingOff, firmware);
  assert.deepEqual(await recoverFirmware(kit, profilingOff), {stock, selected, extensions});
  assert.deepEqual(await patchFirmware(kit, linkKit(kit, selected, null, extensions), stock, {profiling: false}), profilingOff);
  writeFileSync(target + '.profiling-off.ldr', profilingOff);
  const timingHooks = kit.profile.patches.filter(edit => edit.profiling === false);
  assert.ok(timingHooks.length > 1);
  const mixed = profilingOff.slice(), firstHook = timingHooks[0];
  const offset = Number(firstHook.file_offset);
  mixed.set(firmware.subarray(offset, offset + firstHook.length), offset);
  // Not an exact build: stock is still recovered and verified, but no selection.
  const earlier = {stock, selected:null, earlier:true};
  assert.deepEqual(await recoverFirmware(kit, mixed), earlier);
  const damagedLabel = firmware.slice();
  damagedLabel[Number(kit.profile.patches.at(-1).file_offset)] ^= 1;
  assert.deepEqual(await recoverFirmware(kit, damagedLabel), earlier);
  writeFileSync(target + '.ldr', firmware);
  const files = await buildAssets(kit, selected, catalog, linked.numbers, linked.extensions);
  for (const [path, bytes] of Object.entries(files)) {
    const name = resolve(target, path); mkdirSync(resolve(name, '..'), {recursive: true}); writeFileSync(name, bytes);
  }
  writeFileSync(target + '.zip', zipFiles({'os.ldr': firmware, ...files}));
  const changed = stock.slice(); changed[50] ^= 1;
  await assert.rejects(patchFirmware(kit, linked, changed), /Only OP–XY OS 1.2.1 is supported/);
  await assert.rejects(patchFirmware(kit, linked, firmware), /Only OP–XY OS 1.2.1 is supported/);
  // Corruption outside the reviewed edits is not OS 1.2.1. Inside a hook or the
  // module, reversing the edits still recovers verified stock, without a selection.
  const outside = firmware.slice(); outside[50] ^= 1;
  await assert.rejects(recoverFirmware(kit, outside), /not made from OS 1.2.1/);
  for (const offset of [Number(kit.profile.patches[2].file_offset), readLdr(firmware).at(-2).offset + 16]) {
    const damaged = firmware.slice(); damaged[offset] ^= 1;
    assert.deepEqual(await recoverFirmware(kit, damaged), earlier);
  }
}
// Relinking does not depend on checkbox order or mutate the object capsules.
const all = kit.linked ?? kit.machines.map(m => m.id);
assert.deepEqual(linkKit(kit, all).module, linkKit(kit, [...all].reverse()).module);
console.log('Browser container/assets checks: checked selections recovered in both profiling modes; altered builds recover only verified stock; foreign changes rejected.');
