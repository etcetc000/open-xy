// Private release check: the checked selections link identically with GNU ld.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve, dirname} from 'node:path';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {openKit, linkInputs, linkKit, readLayout, sha256, checkedSelections, extensionChoices, selectionBit, memoryKit, offersExpandedMemory, moduleBytes} from '../builder/core.mjs';

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
function gnuLink(memory, prefix, definitions, objects) {
  execFileSync(ld, ['-T', memory.script, '-e', 'extensions_synth_exit', '-o', prefix + '.elf',
    ...definitions.map(([key, value]) => `--defsym=${key}=${value}`),
    ...objects.map(o => resolve(objectDir, o.name))], {stdio: 'pipe', windowsHide: true});
}

const hex = entries => Object.entries(entries).map(([key, value]) => [key, `0x${value.toString(16)}`]);

// Fits must match GNU's bytes. Over-budget optional choices must be rejected by both linkers.
async function parity(memory, mask, selected, extensions, numbering, name) {
  const prefix = resolve(destination, name);
  let linked;
  try {
    linked = linkKit(memory.kit, selected, numbering, extensions);
  } catch (error) {
    if (error.capacity !== true || !extensions.length ||
        (numbering && extensions.length === kit.extensions.length)) throw error;
    const inputs = linkInputs(memory.kit, selected, numbering, extensions);
    const definitions = [...hex(inputs.definitions), ...inputs.fallbacks.map(([key, target]) => [key, target])];
    assert.throws(() => gnuLink(memory, prefix, definitions, inputs.objects), failure => {
      const stderr = failure.stderr?.toString() ?? '';
      return /region `MODULE' overflowed by \d+ bytes|Module overlaps the relocated tables/.test(stderr) &&
        !/undefined reference|relocation truncated|multiple definition/.test(stderr);
    }, `GNU must also reject this over-budget selection: ${selected.join(', ')} with ${extensions.join(', ')}`);
    console.log('Both linkers rejected an over-budget selection' + memory.suffix + ':',
      selected.join(' + '), 'extensions:', extensions.join(','));
    return;
  }
  gnuLink(memory, prefix, [...hex(linked.definitions), ...hex(Object.fromEntries(linked.aliases))], linked.objects);
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
for (const mask of checkedSelections(kit.machines, kit.linked)) {
  const selected = kit.machines.filter((_, i) => mask & selectionBit(i)).map(m => m.id);
  // A mask runs to 78 digits: a short digest names the selection stably and keeps paths short.
  const digest = (await sha256(new TextEncoder().encode(mask.toString()))).slice(0, 16);
  for (const [choice, extensions] of extensionChoices(kit).entries())
  for (const numbering of mask === linkedMask ? [null, native] : [null])
  for (const memory of layouts) await parity(memory, mask, selected, extensions, numbering,
    `${digest}-${choice}` + (numbering ? '-native' : '') + memory.suffix);
}
// Check the whole collection at the larger layout as well when a choice
// exceeds standard memory and fits the larger layout.
if (layouts.length > 1) {
  const all = kit.machines.map(m => m.id);
  // The whole collection's allocated bytes with an extension choice, or null when it does
  // not fit; any other link error is a real defect and fails the check.
  const link = (k, extensions) => { try { return linkKit(k, all, null, extensions).sizes.allocated; }
    catch (error) { if (error.capacity === true) return null; throw error; } };
  // One link per choice, at the larger memory: it needs more than standard memory when
  // its allocation exceeds the standard module region.
  const tried = extensionChoices(kit).map(c => ({c, larger: link(layouts[1].kit, c)}));
  // The choice with the most code that needs more than standard memory and fits the larger.
  const beyond = tried.filter(t => typeof t.larger === 'number' && t.larger > moduleBytes(kit.profile))
    .sort((x, y) => y.larger - x.larger)[0];
  if (!beyond) console.log('Experimental memory: no extension choice with the whole collection needs more than standard memory',
    'and fits the larger - no module beyond standard memory is checked.');
  else {
    const mask = kit.machines.reduce((m, _, i) => m | selectionBit(i), 0n);
    await parity(layouts[1], mask, all, beyond.c, null, 'collection-expanded');
  }
}
for (const memory of layouts) writeFileSync(resolve(destination, memory.file), JSON.stringify(memory.results, null, 2));
