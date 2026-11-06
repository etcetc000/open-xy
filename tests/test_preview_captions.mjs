import test from 'node:test';
import assert from 'node:assert/strict';
import {createControls, countsLikeStock, stockNumber} from '../sdk/browser/controls.mjs';

const definition = {id: 'captions', name: 'Captions', parameters: [
  {id: 'level', label: 'level', default: 0.65, mapping: {kind: 'linear', minimum: 0, maximum: 1}, display: 'percent'},
  {id: 'time', label: 'time', default: 0.5, mapping: {kind: 'linear', minimum: 0, maximum: 1}, display: 'division'},
  {id: 'pitch', label: 'pitch', default: 0.5, mapping: {kind: 'enum', labels: ['-1st', '0st', '+1st']}, display: 'enum'},
]};

test('numbers count 00..99 as header_number does on the device', () => {
  // page_header.hpp: ((value >> 8) * 100) >> 23, negative words reading 00.
  const device = word => word < 0 ? 0 : Number((BigInt(word >>> 8) * 100n) >> 23n);
  for (const word of [-1, 0, 1, 0x147ae14, 0x147ae15, 0x20000000, 0x40000000, 0x53333333, 0x7fffffff]) {
    assert.equal(stockNumber(word), String(device(word)).padStart(2, '0'));
  }
  assert.equal(stockNumber(0), '00');
  assert.equal(stockNumber(0x7fffffff), '99');
  assert.equal(stockNumber(0x07000000), '05');
});

test('the declared SDK picks the captions; names stay names', () => {
  assert.equal(countsLikeStock('1.11'), true);
  assert.equal(countsLikeStock('1.10'), false);
  assert.equal(countsLikeStock('2.0'), true);
  assert.throws(() => countsLikeStock(undefined), /SDK version/);
  const stock = createControls(structuredClone(definition), '1.11');
  const before = createControls(structuredClone(definition), '1.10');
  const value = stock.defaults();
  // 0.65 is a little under 65/100 of the word, as on the device (preset 21299 << 16 reads 64 too).
  assert.equal(stockNumber(21299 << 16), '64');
  assert.deepEqual(stock.parameters.map(p => stock.displayValue(p.id, value)), ['64', '1/8', '0st']);
  assert.deepEqual(before.parameters.map(p => before.displayValue(p.id, value)), ['65.0', '1/8', '0st']);
  assert.equal(stock.displayValue('level', {...value, level: 0}), '00');
  assert.equal(stock.displayValue('level', {...value, level: 1}), '99');
});
