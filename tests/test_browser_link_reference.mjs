import test from 'node:test';
import assert from 'node:assert/strict';
import {capacityInputs, requireCapacityRejection} from '../tools/browser_link_reference.mjs';

function rejection(excess=12) {
  return {status:1, signal:null, stdout:Buffer.alloc(0), stderr:Buffer.from(
    "ld: result.elf section `.bss' will not fit in region `MODULE'\n" +
    'ld: Module overlaps the relocated tables\n' +
    "ld: region `MODULE' overflowed by " + excess + ' bytes\n')};
}
test('capacity qualification requires the GNU boundary error and exact overflow size', () => {
  requireCapacityRejection(rejection(), 12);
  assert.throws(() => requireCapacityRejection(rejection(13), 12), /disagree/);
  for (const change of [{status:0}, {status:null}, {signal:'SIGTERM'}, {stdout:Buffer.from('unexpected')},
    {stderr:Buffer.from('ld: undefined reference to missing')}, {stderr:Buffer.from('')},
    {stderr:Buffer.from("ld: region `MODULE' overflowed by 12 bytes")},
    {stderr:Buffer.concat([rejection().stderr, Buffer.from('ld: undefined reference to missing')])}]) {
    assert.throws(() => requireCapacityRejection({...rejection(), ...change}, 12));
  }
});
test('capacity reference rebases module fallbacks without moving stock addresses or changing inputs', () => {
  const expanded = {module_base:0x1000, reserved_bytes:0x400, table_bytes:0};
  const standard = {module_base:0x1200, reserved_bytes:0x200, table_bytes:0};
  const linked = {sizes:{allocated:0x240}, aliases:new Map([['absent',0x1010],['stock',0x5000]])};
  const inputs = capacityInputs(linked, expanded, standard);
  assert.deepEqual([...inputs.aliases], [['absent',0x1210],['stock',0x5000]]);
  assert.equal(linked.aliases.get('absent'), 0x1010);
  assert.throws(() => capacityInputs({...linked,sizes:{allocated:0x200}}, expanded, standard), /exceed standard/);
});
