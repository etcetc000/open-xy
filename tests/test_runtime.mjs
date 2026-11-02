import test from 'node:test';
import assert from 'node:assert/strict';
import {selectRuntime} from '../builder/core.mjs';

function object(name, exports = [], imports = []) {
  return {name, symbols:[...exports.map(name => ({name, binding:1, section:1, type:2})),
    ...imports.map(name => ({name, binding:1, section:0, type:2}))]};
}

test('arithmetic closure is lazy, deterministic, transitive and honors SDK definitions', () => {
  const candidates = [object('sdk/runtime/_muldi3.o', ['___muldi3'], ['___clzsi2', '___divsi3']),
    object('sdk/runtime/_divsi3.o', ['___divsi3']), object('sdk/runtime/_clzsi2.o', ['___clzsi2'])];
  const consumer = object('machine.o', ['entry', '___divsi3'], ['___muldi3']);
  assert.deepEqual(selectRuntime([consumer], candidates).map(o => o.name),
    ['sdk/runtime/_clzsi2.o', 'sdk/runtime/_muldi3.o']);
  assert.deepEqual(selectRuntime([object('integer.o')], candidates), []);
  assert.deepEqual(selectRuntime([consumer], candidates, {___muldi3:123}), []);
  assert.deepEqual(selectRuntime([consumer], [...candidates].reverse()), selectRuntime([consumer], candidates));
});

test('arithmetic members cannot silently replace definitions or introduce weak symbols', () => {
  assert.throws(() => selectRuntime([object('machine.o', ['owned'], ['___muldi3'])],
    [object('sdk/runtime/_muldi3.o', ['owned', '___muldi3'])]), /replaces an existing definition/);
  const weak = object('sdk/runtime/_muldi3.o', ['___muldi3']);
  weak.symbols[0].binding = 2;
  assert.throws(() => selectRuntime([], [weak]), /Weak object symbols/);
});
