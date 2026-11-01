import test from 'node:test';
import assert from 'node:assert/strict';
import {BUSY, Device, container, objectInfo, ptpString, readString} from '../builder/mtp.mjs';

import {fakeDevice} from './fixtures/mtp.mjs';

test('regular OP-XY still-image MTP supports collection write and readback', async () => {
  const {device, state} = fakeDevice({files: {'projects/existing.xy': Uint8Array.of(7)}});
  device.vendorId = 0x2367; device.productId = 0x0021;
  await device.selectConfiguration(1);
  Object.assign(device.configuration.interfaces[0].alternate,
    {interfaceClass: 6, interfaceSubclass: 1, interfaceProtocol: 1});
  const mtp = new Device(device), bytes = Uint8Array.of(1, 2, 3);
  await mtp.open();
  await mtp.send('projects/openxy/collection.oxc', bytes);
  assert.deepEqual(await mtp.read('projects/openxy/collection.oxc'), bytes);
  await mtp.close();
  assert.deepEqual(state.read('projects/existing.xy'), Uint8Array.of(7));
});

test('still-image interfaces require the exact OP-XY and MTP interface descriptors', async () => {
  for (const change of [{productId: 1}, {vendorId: 1}, {interfaceSubclass: 2}, {interfaceProtocol: 2}]) {
    const {device, state} = fakeDevice();
    device.vendorId = 0x2367; device.productId = 0x0021;
    await device.selectConfiguration(1);
    const alternate = device.configuration.interfaces[0].alternate;
    Object.assign(alternate, {interfaceClass: 6, interfaceSubclass: 1, interfaceProtocol: 1});
    for (const [key, value] of Object.entries(change))
      (key.startsWith('interface') ? alternate : device)[key] = value;
    await assert.rejects(new Device(device).open(), /MTP/);
    assert.deepEqual(state.ops, []);
    assert.equal(state.opened, false);
  }
});

test('reads the original OS in chunks without using any write operations', async () => {
  const path = 'upgrade/firmware/blackfin/bin/os.ldr';
  const bytes = Uint8Array.from({length: 3 * 1024 * 1024 - 12}, (_, i) => i * 13);
  const {device, state} = fakeDevice({files: {[path]: bytes, 'os.ldr': Uint8Array.of(99)}, split: true});
  const mtp = new Device(device), progress = [];
  await mtp.open();
  assert.deepEqual(await mtp.read(path, {maxBytes: bytes.length, onProgress: n => progress.push(n)}), bytes);
  await mtp.close();
  assert.equal(progress.at(-1), 1);
  assert.ok(progress.length > 2 && progress.every((n, i) => n >= 0 && n <= 1 && (!i || n >= progress[i - 1])));
  assert.ok(state.ops.every(code => ['1002', '1003', '1004', '1007', '1008', '1009'].includes(code)));
  assert.deepEqual(state.read('os.ldr'), Uint8Array.of(99));
  assert.equal(state.opened, false);
});

test('reading rejects missing paths, folders and oversized files without creating anything', async () => {
  const {device, state} = fakeDevice({files: {'folder/file': new Uint8Array(20)}});
  const mtp = new Device(device); await mtp.open();
  for (const [path, options] of [['missing/os.ldr', {}], ['folder', {}], ['folder/file/child', {}],
    ['../os.ldr', {}], ['folder/file', {maxBytes: 10}]]) await assert.rejects(mtp.read(path, options));
  assert.deepEqual(state.paths(), ['folder/', 'folder/file']);
  assert.ok(!state.ops.includes('1009') && !state.ops.includes('100c') && !state.ops.includes('100b'));
  await mtp.close();
});

test('rejects mismatched transactions, operation codes and malformed file sizes', async () => {
  for (const corrupt of [
    bytes => new DataView(bytes.buffer).setUint32(8, 999, true),
    bytes => new DataView(bytes.buffer).setUint16(6, 0x1008, true),
    bytes => new DataView(bytes.buffer).setUint32(0, 0xffffffff, true),
    bytes => new DataView(bytes.buffer).setUint32(0, 11, true),
  ]) {
    const {device} = fakeDevice({files: {'os.ldr': new Uint8Array(32)}, corrupt});
    const mtp = new Device(device); await mtp.open();
    await assert.rejects(mtp.read('os.ldr', {maxBytes: 32}), /Unexpected/);
    await mtp.close();
  }
});

test('cancels a stalled read and releases the device without altering files', async () => {
  const {device, state} = fakeDevice({files: {'os.ldr': new Uint8Array(32)}, hang: 0x1009});
  const mtp = new Device(device); await mtp.open();
  const reading = mtp.read('os.ldr');
  const rejected = assert.rejects(reading, /cancelled/);
  while (!state.abort) await new Promise(resolve => setTimeout(resolve, 0));
  await mtp.close(); await rejected;
  assert.equal(state.control.request, 0x64);
  assert.deepEqual(state.deleted, []); assert.equal(state.opened, false);
});

const stock = {'os.ldr': Uint8Array.of(1), 'content/op1_factory.json': Uint8Array.of(1), 'content/display/stock.svg': Uint8Array.of(7),
  'content/presets/stock/default.json': Uint8Array.of(7), 'content/samples': null, 'user': null};

test('PTP strings round trip and match the wire layout', () => {
  assert.deepEqual([...ptpString('')], [0]);
  assert.deepEqual([...ptpString('os.ldr')], [7, 111,0,115,0,46,0,108,0,100,0,114,0,0,0]);
  assert.equal(readString(ptpString('Ünïcode name.json'), 0), 'Ünïcode name.json');
  assert.throws(() => ptpString('x'.repeat(255)), /too long/);
});

test('sends a file to the top level with the parent the OP–XY accepts', async () => {
  const {device, state} = fakeDevice({split: true}), mtp = new Device(device);
  const bytes = Uint8Array.from({length: 3 * 1024 * 1024 + 5}, (_, i) => i * 7);
  const progress = [];
  await mtp.open();
  assert.equal(await mtp.send('os.ldr', bytes, {onProgress: p => progress.push(p)}), true);
  assert.deepEqual(await mtp.read('os.ldr'), bytes);
  await mtp.close();
  assert.deepEqual(state.read('os.ldr'), bytes);
  assert.deepEqual(state.ops, ['1002', '1004', '1007', '100c', '100d', '1009', '1003']);
  assert.equal(progress.at(-1), 1);
  assert.equal(state.opened, false); assert.equal(state.claimed, null);
});

test('sends a whole build into existing folders, replacing files and keeping other content', async () => {
  const {device, state} = fakeDevice({files: stock}), mtp = new Device(device);
  const build = {'os.ldr': Uint8Array.of(2), 'layout.json': Uint8Array.of(2), 'content/op1_factory.json': Uint8Array.of(2),
    'content/display/macroosc.svg': Uint8Array.of(2), 'content/presets/macroosc/default.json': Uint8Array.of(2),
    'content/presets/fm2op/default.json': Uint8Array.of(2)};
  const asked = [], steps = [];
  await mtp.open();
  for (const [path, bytes] of Object.entries(build)) {
    assert.equal(await mtp.send(path, bytes, {replace: async p => { asked.push(p); return true; }, onStep: (...step) => steps.push(step.join(' '))}), true);
  }
  await mtp.close();
  assert.deepEqual(steps, ['replace os.ldr', 'replace content/op1_factory.json', 'folder content/presets/macroosc', 'folder content/presets/fm2op']);
  // Each existing folder is listed once: the top level, content, display and presets.
  assert.equal(state.ops.filter(op => op === '1007').length, 4);
  for (const [path, bytes] of Object.entries(build)) assert.deepEqual(state.read(path), bytes, path);
  assert.deepEqual(asked, ['os.ldr', 'content/op1_factory.json']);
  assert.deepEqual(state.paths(), ['content/', 'content/display/', 'content/display/macroosc.svg', 'content/display/stock.svg',
    'content/op1_factory.json', 'content/presets/', 'content/presets/fm2op/', 'content/presets/fm2op/default.json',
    'content/presets/macroosc/', 'content/presets/macroosc/default.json', 'content/presets/stock/',
    'content/presets/stock/default.json', 'content/samples/', 'layout.json', 'os.ldr', 'user/']);
  assert.deepEqual(state.read('content/display/stock.svg'), Uint8Array.of(7));
});

test('ends a packet-aligned transfer with a zero-length packet', async () => {
  // 12-byte data header + 500 bytes = exactly one 512-byte packet.
  const {device, state} = fakeDevice(), mtp = new Device(device);
  await mtp.open(); await mtp.send('aligned.bin', new Uint8Array(500)); await mtp.close();
  assert.equal(state.zeroLength, 1);
  assert.equal(state.read('aligned.bin').length, 500);
});

test('asks before replacing a file and never replaces a folder', async () => {
  const {device, state} = fakeDevice({files: stock}), mtp = new Device(device);
  await mtp.open();
  assert.equal(await mtp.send('content/op1_factory.json', Uint8Array.of(2)), false);
  assert.deepEqual([state.deleted, state.read('content/op1_factory.json')], [[], Uint8Array.of(1)]);
  await assert.rejects(mtp.send('content/samples', Uint8Array.of(3), {replace: async () => true}), /already has a folder named content\/samples/);
  await assert.rejects(mtp.send('os.ldr/inner.txt', Uint8Array.of(3)), /file named os\.ldr where a folder is needed/);
  assert.equal(state.read('content/samples'), null);
  await mtp.close();
});

test('explains a device held by another app and releases it', async () => {
  const {device, state} = fakeDevice({busy: true});
  await assert.rejects(new Device(device).open(), error => error.message === BUSY);
  assert.equal(state.opened, false);
});

test('reports device refusals in plain language', async () => {
  const {device} = fakeDevice({refuse: {code: 0x100c, response: 0x200c}}), mtp = new Device(device);
  await mtp.open();
  await assert.rejects(mtp.send('big.bin', Uint8Array.of(1)), /full/);
  await mtp.close();
  const other = fakeDevice({refuse: {code: 0x100d, response: 0x2002}}), second = new Device(other.device);
  await second.open();
  await assert.rejects(second.send('content/x.bin', Uint8Array.of(1)), /couldn’t save content\/x\.bin \(MTP error 0x2002\)/);
  await second.close();
});

test('rejects paths that would leave the device or name nothing', async () => {
  const {device} = fakeDevice(), mtp = new Device(device);
  await mtp.open();
  for (const path of ['', '/os.ldr', 'content//x', '../os.ldr', 'content/../os.ldr', 'content/./x', 'a\\b', 'a\0b', 'content/']) {
    await assert.rejects(mtp.send(path, Uint8Array.of(1)), /isn’t a file path/, JSON.stringify(path));
  }
  await mtp.close();
});

test('stopping mid-file cancels the transfer instead of leaving the device waiting', async () => {
  const {device, state} = fakeDevice({hang: 0x100d}), mtp = new Device(device);
  await mtp.open();
  const sending = mtp.send('os.ldr', new Uint8Array(2048));
  while (!state.ops.includes('100d')) await new Promise(resolve => setImmediate(resolve));
  await mtp.close();
  await assert.rejects(sending);
  // Class Cancel Request (0x64) naming the SendObject transaction, then both endpoints cleared.
  assert.deepEqual([state.control.requestType, state.control.recipient, state.control.request, state.control.index], ['class', 'interface', 0x64, 0]);
  const transaction = state.ops.length - 1;
  assert.deepEqual(state.control.data, [0x01, 0x40, transaction, 0, 0, 0]);
  assert.equal(state.statusPolls, 2, 'waits for the device to finish cancelling');
  assert.equal(state.halts, undefined, 'healthy endpoints are left alone');
  assert.ok(!state.ops.includes('1003'), 'no CloseSession while a transfer is pending');
  assert.equal(state.opened, false);
});

test('a second session after a clean close still gets going', async () => {
  // The OS read closes, then installation opens again on the same device.
  const {device, state} = fakeDevice({files: {'os.ldr': Uint8Array.of(1)}, strictToggle: true});
  const first = new Device(device);
  await first.open(); assert.deepEqual(await first.read('os.ldr'), Uint8Array.of(1)); await first.close();
  const second = new Device(device);
  await second.open(); assert.equal(await second.send('layout.json', Uint8Array.of(2)), true); await second.close();
  assert.equal(state.dropped, undefined, 'no packet lost to a reset data toggle');
  assert.equal(state.halts, undefined);
});

test('a stalled endpoint is cleared and the transfer retried once', async () => {
  const {device, state} = fakeDevice({stallOnce: 'out'}), mtp = new Device(device);
  await mtp.open(); assert.equal(await mtp.send('os.ldr', Uint8Array.of(3)), true); await mtp.close();
  assert.deepEqual(state.halts, ['out 10']);
  assert.deepEqual(state.read('os.ldr'), Uint8Array.of(3));
});
