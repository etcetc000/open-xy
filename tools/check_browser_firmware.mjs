// Private input check. Emits only into the maintainer's ignored output directory.
import {existsSync, readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
import {openKit, linkKit, patchFirmware, recoverFirmware, readLdr, buildAssets, zipFiles, checkedSelections, imageSettings, memoryKit, moduleBytes, offersExpandedMemory, sha256, RECOVERABLE_MACHINES} from '../builder/core.mjs';
// The settings recovery reports for a build: the image's own, and each unit capability's value
// (fast_helpers) when a selected extension carries it, which no image site shows.
const recoveredSettings = (linked, firmware) => {
  const carried = Object.entries(linked.units ?? {}).filter(([c]) => (kit.extensions ?? [])
    .some(e => linked.extensions.includes(e.id) && (e.capabilities ?? []).includes(c)));
  const held = {...imageSettings(kit, firmware), ...Object.fromEntries(carried)};
  return Object.keys(held).length ? {settings: held} : {};
};
const [directory, stockFile, content] = process.argv.slice(2);
const kit = await openKit(JSON.parse(readFileSync(resolve(directory, 'kit.json'))));
const stock = new Uint8Array(readFileSync(stockFile));
const catalog = new Uint8Array(readFileSync(resolve(content, 'op1_factory.json')));
assert.deepEqual(await recoverFirmware(kit, stock), {stock, selected:null});
const selections = JSON.parse(readFileSync(resolve(directory, 'reference/selections.json')));
const rejected = JSON.parse(readFileSync(resolve(directory, 'reference/capacity-rejections.json')));
if (rejected.length) {
  assert.ok(offersExpandedMemory(kit.profile), 'Capacity rejections require a checked larger layout');
  const expanded = JSON.parse(readFileSync(resolve(directory, 'reference/selections-expanded.json')));
  for (const result of rejected) {
    assert.throws(() => linkKit(kit, result.selected, result.layout, result.extensions, result.settings), error => error.capacity === true);
    assert.ok(!selections.some(r => r.name === result.name), 'Rejected selection produced standard firmware');
    const larger = expanded.filter(r => r.name === result.name + '-expanded');
    assert.equal(larger.length, 1, 'Capacity rejection lacks expanded GNU parity');
    for (const key of ['selected', 'extensions', 'layout', 'settings']) assert.deepEqual(larger[0][key], result[key]);
    assert.equal(larger[0].sizes.allocated - moduleBytes(kit.profile), result.excess);
  }
}
// A selection numbered with a layout (the native package's own) is recovered with it.
for (const {name, selected, extensions, layout, settings: chosen = {}} of selections) {
  const linked = linkKit(kit, selected, layout, extensions, chosen), target = resolve(directory, 'reference', name);
  const firmware = await patchFirmware(kit, linked, stock, {settings: chosen});
  const recovered = await recoverFirmware(kit, firmware, layout);
  // Capability settings an image holds come back with its selection.
  const settings = recoveredSettings(linked, firmware);
  assert.deepEqual(recovered, {stock, selected, extensions, memory: 'standard', ...settings});
  assert.deepEqual(await patchFirmware(kit, linkKit(kit, recovered.selected, layout, recovered.extensions, recovered.settings),
    recovered.stock, {settings: recovered.settings}), firmware);
  const profilingOff = await patchFirmware(kit, linked, stock, {profiling: false, settings: chosen});
  assert.notDeepEqual(profilingOff, firmware);
  assert.deepEqual(await recoverFirmware(kit, profilingOff, layout), {stock, selected, extensions, memory: 'standard', ...settings});
  assert.deepEqual(await patchFirmware(kit, linkKit(kit, selected, layout, extensions, chosen), stock, {profiling: false, settings: chosen}), profilingOff);
  writeFileSync(target + '.profiling-off.ldr', profilingOff);
  const timingHooks = kit.profile.patches.filter(edit => edit.profiling === false);
  assert.ok(timingHooks.length > 1);
  const mixed = profilingOff.slice(), firstHook = timingHooks[0];
  const offset = Number(firstHook.file_offset);
  mixed.set(firmware.subarray(offset, offset + firstHook.length), offset);
  // Not an exact build: stock is still recovered and verified, but no selection.
  const earlier = {stock, selected:null, earlier:true, memory: 'standard'};
  assert.deepEqual(await recoverFirmware(kit, mixed, layout), earlier);
  const damagedLabel = firmware.slice();
  damagedLabel[Number(kit.profile.patches.at(-1).file_offset)] ^= 1;
  assert.deepEqual(await recoverFirmware(kit, damagedLabel, layout), earlier);
  writeFileSync(target + '.ldr', firmware);
  const files = await buildAssets(kit, selected, catalog, linked.numbers, linked.extensions);
  for (const [path, bytes] of Object.entries(files)) {
    const name = resolve(target, path); mkdirSync(resolve(name, '..'), {recursive: true}); writeFileSync(name, bytes);
  }
  writeFileSync(target + '.zip', zipFiles({'os.ldr': firmware, ...files}));
  const changed = stock.slice(); changed[50] ^= 1;
  await assert.rejects(patchFirmware(kit, linked, changed, {settings: chosen}), /Only OP–XY OS 1.2.1 is supported/);
  await assert.rejects(patchFirmware(kit, linked, firmware, {settings: chosen}), /Only OP–XY OS 1.2.1 is supported/);
  // Corruption outside the reviewed edits is not OS 1.2.1. Inside a hook or the
  // module, reversing the edits still recovers verified stock, without a selection.
  const outside = firmware.slice(); outside[50] ^= 1;
  await assert.rejects(recoverFirmware(kit, outside, layout), /not made from OS 1.2.1/);
  for (const offset of [Number(kit.profile.patches[2].file_offset), readLdr(firmware).at(-2).offset + 16]) {
    const damaged = firmware.slice(); damaged[offset] ^= 1;
    assert.deepEqual(await recoverFirmware(kit, damaged, layout), earlier);
  }
}
// Experimental larger memory: each GNU-checked selection patches the real stock OS,
// writes the smaller heap and is recovered exactly, with the option turned on.
const expandedFile = resolve(directory, 'reference/selections-expanded.json');
assert.equal(existsSync(expandedFile), offersExpandedMemory(kit.profile));
if (offersExpandedMemory(kit.profile)) {
  const expanded = memoryKit(kit, true);
  for (const {name, selected, extensions, layout, module_sha256, settings: chosen = {}} of JSON.parse(readFileSync(expandedFile))) {
    const linked = linkKit(expanded, selected, layout, extensions, chosen);
    assert.equal(await sha256(linked.module), module_sha256);
    for (const profiling of [true, false]) {
      const firmware = await patchFirmware(expanded, linked, stock, {profiling, settings: chosen});
      assert.equal(readLdr(firmware).at(-2).target, expanded.profile.module_base);
      for (const edit of expanded.profile.patches.filter(e => e.kind === 'heap'))
        assert.equal(Buffer.from(firmware.subarray(Number(edit.file_offset), Number(edit.file_offset) + edit.length)).toString('hex'), edit.after);
      // A collection too large to search exhaustively may recover only its stock OS;
      // its memory is always known. A small one is always recovered exactly.
      const recovered = await recoverFirmware(kit, firmware, layout);
      const settings = recoveredSettings(linked, firmware);
      assert.deepEqual(recovered, name === 'collection-expanded' && kit.machines.length > RECOVERABLE_MACHINES && recovered.earlier
        ? {stock, selected: null, earlier: true, memory: 'expanded'} : {stock, selected, extensions, memory: 'expanded', ...settings});
      await assert.rejects(patchFirmware(kit, linked, stock, {profiling, settings: chosen}), /outside module|exceeds its profile/);
      writeFileSync(resolve(directory, 'reference', name) + (profiling ? '.ldr' : '.profiling-off.ldr'), firmware);
    }
  }
}
// Relinking does not depend on checkbox order or mutate the object capsules.
const all = kit.linked ?? kit.machines.map(m => m.id);
assert.deepEqual(linkKit(kit, all).module, linkKit(kit, [...all].reverse()).module);
console.log('Browser container/assets checks: checked selections recovered in both profiling modes; altered builds recover only verified stock; foreign changes rejected.');
