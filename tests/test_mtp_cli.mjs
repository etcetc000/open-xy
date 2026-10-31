import test from 'node:test';
import assert from 'node:assert/strict';
import {Device} from '../builder/mtp.mjs';
import {Session, xyDevice} from '../tooling/mtp_cli.mjs';
import {fakeDevice} from './fixtures/mtp.mjs';

test('selects exactly one XY, never another Teenage Engineering device', () => {
  const xy = {vendorId: 0x2367, productId: 0x0021};
  const other = {vendorId: 0x2367, productId: 0x0001};
  assert.equal(xyDevice([other, xy]), xy);
  assert.throws(() => xyDevice([other]), /one OP-XY/);
  assert.throws(() => xyDevice([xy, {...xy}]), /one OP-XY/);
});

test('CLI bridge uses browser MTP for fresh listings, file readback and replacement', async () => {
  const {device, state} = fakeDevice({files: {'os.ldr': Uint8Array.of(1, 2, 3)}});
  const session = new Session(new Device(device));
  const run = (op, args = {}) => session.dispatch({op, ...args});
  await run('open');
  const [old] = await run('list', {parent: 'root'});
  assert.equal(old.name, 'os.ldr');
  assert.equal(await run('read', {id: old.id}), 'AQID');
  await assert.rejects(run('write', {parent: 'root', name: 'os.ldr', data: 'BAU='}), /appeared/);
  await run('delete', {id: old.id});
  assert.deepEqual(await run('list', {parent: 'root'}), []);
  await run('write', {parent: 'root', name: 'os.ldr', data: 'BAU='});
  const [next] = await run('list', {parent: 'root'});
  assert.notEqual(next.id, old.id);
  assert.equal(await run('read', {id: next.id}), 'BAU=');
  await run('mkdir', {parent: 'root', name: 'content'});
  const folder = (await run('list', {parent: 'root'})).find(x => x.folder);
  await assert.rejects(run('delete', {id: folder.id}), /folder/);
  await run('write', {parent: folder.id, name: 'test.svg', data: 'Bgc='});
  const [file] = await run('list', {parent: folder.id});
  assert.equal(await run('read', {id: file.id}), 'Bgc=');
  await run('delete', {id: file.id});
  assert.deepEqual(await run('list', {parent: folder.id}), []);
  await run('close');
  assert.equal(state.opened, false);
  assert.deepEqual(state.read('os.ldr'), Uint8Array.of(4, 5));
});
