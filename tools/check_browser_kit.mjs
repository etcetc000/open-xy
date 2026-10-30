// Private release check: the checked selections link identically with GNU ld.
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve, dirname} from 'node:path';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {openKit, linkKit, sha256, checkedSelections, extensionChoices, selectionBit} from '../builder/core.mjs';

const [kitFile, objectDir, ld, objcopy] = process.argv.slice(2);
const kit = await openKit(JSON.parse(readFileSync(kitFile, 'utf8')));
const destination = resolve(dirname(kitFile), 'reference'); mkdirSync(destination);
const layout = resolve('linker/opxy-1.2.1.ld');
const results = [];
for (const mask of checkedSelections(kit.machines, kit.linked)) {
  const selected = kit.machines.filter((_, i) => mask & selectionBit(i)).map(m => m.id);
  // A mask runs to 78 digits: a short digest names the selection stably and keeps paths short.
  const digest = (await sha256(new TextEncoder().encode(mask.toString()))).slice(0, 16);
  for (const [choice, extensions] of extensionChoices(kit).entries()) {
    const name = `${digest}-${choice}`;
    const linked = linkKit(kit, selected, null, extensions), prefix = resolve(destination, name);
    const definitions = [...Object.entries(linked.definitions), ...linked.aliases.entries()]
      .map(([key, value]) => `--defsym=${key}=0x${value.toString(16)}`);
    execFileSync(ld, ['-T', layout, '-e', 'extensions_synth_exit', '-o', prefix + '.elf', ...definitions,
      ...linked.objects.map(o => resolve(objectDir, o.name))], {stdio: 'pipe', windowsHide: true});
    execFileSync(objcopy, ['-O', 'binary', prefix + '.elf', prefix + '.bin'], {stdio: 'pipe', windowsHide: true});
    const actual = readFileSync(prefix + '.bin');
    assert.deepEqual(Buffer.from(linked.module), actual, 'GNU linker mismatch: ' + selected.join(', '));
    // Export exact symbol addresses for independent Python LDR assembly too.
    const result = {name, mask: mask.toString(), selected, extensions, sizes: linked.sizes, globals: Object.fromEntries(linked.globals),
      module_sha256: await sha256(linked.module)};
    writeFileSync(prefix + '.json', JSON.stringify(result)); results.push(result);
    console.log('GNU link parity:', selected.join(' + '), 'extensions:', extensions.join(',') || 'none', linked.sizes.allocated, 'bytes');
  }
}
writeFileSync(resolve(destination, 'selections.json'), JSON.stringify(results, null, 2));
