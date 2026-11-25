// GNU reference-link support for the private release checker.
import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {execFileSync} from 'node:child_process';
import {moduleBytes} from '../builder/core.mjs';

export function referenceLink(ld, script, prefix, linked, objectDir) {
  const definitions = [...Object.entries(linked.definitions), ...linked.aliases.entries()]
    .map(([key, value]) => `--defsym=${key}=0x${value.toString(16)}`);
  const args = ['-T', script, '-e', 'extensions_synth_exit', '-o', prefix + '.elf', ...definitions,
    // A unit capability's other value links its own objects (core.mjs linkKit), kept apart by path.
    ...linked.objects.map(o => resolve(objectDir, o.path ?? o.name))];
  // The catalog can exceed Windows' command-line limit even with short output paths.
  const response = prefix + '.args';
  writeFileSync(response, args.map(arg => '"' + arg.replaceAll('\\', '\\\\').replaceAll('"', '\\"') + '"').join('\n'));
  execFileSync(ld, ['@' + response], {stdio:'pipe', windowsHide:true});
}

export function capacityInputs(linked, expanded, standard) {
  assert.ok(linked.sizes.allocated > moduleBytes(standard), 'Expanded selection must exceed standard memory');
  // Only module-local fallback addresses move; stock addresses remain unchanged.
  const delta = standard.module_base - expanded.module_base;
  return {...linked, aliases:new Map([...linked.aliases].map(([name, value]) => [name,
    value >= expanded.module_base && value < expanded.module_base + linked.sizes.allocated ? value + delta : value]))};
}

export function requireCapacityRejection(error, excess) {
  assert.equal(error.status, 1, 'GNU must reject the oversized selection');
  assert.ok(!error.signal && !error.stdout?.length, 'Unexpected GNU termination or output');
  const lines = String(error.stderr ?? '').trim().split(/\r?\n/);
  const overflow = /: region [`']MODULE' overflowed by (\d+) bytes$/;
  const bounds = /: Module overlaps the relocated tables$/;
  const section = /: .* section [`'][^']+' will not fit in region [`']MODULE'$/;
  assert.ok(lines.length && lines.every(line => overflow.test(line) || bounds.test(line) || section.test(line)),
    'GNU failed for a reason other than module capacity: ' + lines.join('\n'));
  assert.ok(lines.some(line => bounds.test(line)), 'GNU must enforce the relocated-table boundary');
  const sizes = lines.map(line => line.match(overflow)).filter(Boolean);
  assert.equal(sizes.length, 1, 'Missing or ambiguous GNU overflow size');
  assert.equal(Number(sizes[0][1]), excess, 'Browser and GNU disagree on the overflow size');
}
