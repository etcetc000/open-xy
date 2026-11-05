// Private release check: the checked selections link identically with GNU ld.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve, dirname} from 'node:path';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {openKit, linkKit, readLayout, sha256, checkedSelections, extensionChoices, selectionBit, memoryKit, offersExpandedMemory} from '../builder/core.mjs';

const [kitFile, objectDir, ld, objcopy, packageLayout] = process.argv.slice(2);
const kit = await openKit(JSON.parse(readFileSync(kitFile, 'utf8')));
// Every selection is numbered as a new build is, without a layout. The selection of the
// machines the native package links is also numbered with the package's layout, as loading
// that layout.json in the builder would, so it reproduces the package exactly.
const native = readLayout(kit, JSON.parse(readFileSync(packageLayout, 'utf8'))).machines;
const linkedMask = kit.machines.reduce((mask, m, i) => kit.linked.includes(m.id) ? mask | selectionBit(i) : mask, 0n);
const destination = resolve(dirname(kitFile), 'reference'); mkdirSync(destination);
const layout = resolve('linker/opxy-1.2.1.ld');
// The experimental larger memory links the same objects at its lower base: its
// script is the reviewed one with only the module region moved and grown.
const memoryLine = p => `MEMORY { MODULE (rwx) : ORIGIN = 0x${p.module_base.toString(16)}, LENGTH = 0x${(p.reserved_bytes - p.table_bytes).toString(16)} }`;
const layouts = [{suffix: '', kit, script: layout, results: [], file: 'selections.json'}];
if (offersExpandedMemory(kit.profile)) {
  const reviewed = readFileSync(layout, 'utf8'), expanded = memoryKit(kit, true);
  assert.equal(reviewed.split(memoryLine(kit.profile)).length, 2, 'Linker module region differs from the profile');
  const script = resolve(destination, 'opxy-1.2.1-experimental-memory.ld');
  writeFileSync(script, reviewed.replace(memoryLine(kit.profile), memoryLine(expanded.profile)));
  layouts.push({suffix: '-expanded', kit: expanded, script, results: [], file: 'selections-expanded.json'});
}
for (const mask of checkedSelections(kit.machines, kit.linked)) {
  const selected = kit.machines.filter((_, i) => mask & selectionBit(i)).map(m => m.id);
  // A mask runs to 78 digits: a short digest names the selection stably and keeps paths short.
  const digest = (await sha256(new TextEncoder().encode(mask.toString()))).slice(0, 16);
  for (const [choice, extensions] of extensionChoices(kit).entries())
  for (const numbering of mask === linkedMask ? [null, native] : [null])
  for (const memory of layouts) {
    const name = `${digest}-${choice}` + (numbering ? '-native' : '') + memory.suffix;
    const linked = linkKit(memory.kit, selected, numbering, extensions), prefix = resolve(destination, name);
    const definitions = [...Object.entries(linked.definitions), ...linked.aliases.entries()]
      .map(([key, value]) => `--defsym=${key}=0x${value.toString(16)}`);
    execFileSync(ld, ['-T', memory.script, '-e', 'extensions_synth_exit', '-o', prefix + '.elf', ...definitions,
      ...linked.objects.map(o => resolve(objectDir, o.name))], {stdio: 'pipe', windowsHide: true});
    execFileSync(objcopy, ['-O', 'binary', prefix + '.elf', prefix + '.bin'], {stdio: 'pipe', windowsHide: true});
    const actual = readFileSync(prefix + '.bin');
    assert.deepEqual(Buffer.from(linked.module), actual, 'GNU linker mismatch: ' + selected.join(', '));
    // Export exact symbol addresses for independent Python LDR assembly too.
    const result = {name, mask: mask.toString(), selected, extensions, layout: numbering, sizes: linked.sizes, globals: Object.fromEntries(linked.globals),
      module_sha256: await sha256(linked.module)};
    writeFileSync(prefix + '.json', JSON.stringify(result)); memory.results.push(result);
    console.log('GNU link parity' + (memory.suffix && ' (experimental memory)') + ':', selected.join(' + '),
      'extensions:', extensions.join(',') || 'none', linked.sizes.allocated, 'bytes');
  }
}
for (const memory of layouts) writeFileSync(resolve(destination, memory.file), JSON.stringify(memory.results, null, 2));
