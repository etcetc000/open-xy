import test from 'node:test';
import assert from 'node:assert/strict';
import {changes, sourceFiles, encodeFolder, isXYIdentity, enterRegularMTP, Client, withRegularMTP, installedVersion, canSelectRelease, releaseAction, auditionModule} from '../builder/manager.mjs';

test('audition compiles only the authenticated browser module and rejects changed bytes', async () => {
  const bytes = Uint8Array.from([0,97,115,109,1,0,0,0]);
  const hash = [...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(n=>n.toString(16).padStart(2,'0')).join('');
  const preview = {kind:'effect',wasm:btoa(String.fromCharCode(...bytes)),wasm_sha256:hash};
  assert.ok(await auditionModule(preview) instanceof WebAssembly.Module);
  await assert.rejects(auditionModule({...preview,wasm_sha256:'0'.repeat(64)}),/verification failed/);
  await assert.rejects(auditionModule({...preview,kind:'sample'}),/Invalid audition/);
});

test('machine changes preserve installed machines unless explicitly unchecked', () => {
  const installed = [{id:'keep'}, {id:'remove'}];
  assert.deepEqual(changes(installed, new Set(['keep', 'catalog', 'own']), new Map([['own', {}]])),
    {remove:['remove'], add:['catalog'], local:['own'], update:[]});
  assert.deepEqual(changes(installed, new Set(['keep', 'remove']), new Map()), {remove:[], add:[], local:[], update:[]});
});

test('updates are explicit, retained machines remain selected, and unchecked updates are omitted', () => {
  assert.deepEqual(changes([{id:'keep'}, {id:'update'}, {id:'remove'}], new Set(['keep','update']), new Map(), new Set(['update','remove'])),
    {remove:['remove'], add:[], local:[], update:['update']});
  assert.equal(installedVersion({source:{tag:'v1.2',commit:'ab'.repeat(20)}}),'v1.2');
  assert.equal(installedVersion({source:{fingerprint:'cd'.repeat(32)}}),'Saved build cdcdcdcd');
  assert.match(installedVersion({}),/version unknown/);
  assert.equal(canSelectRelease({compatibility:{state:'sdk-compatible'}}),true);
  for (const state of ['firmware-update-required','unavailable','unchecked'])
    assert.equal(canSelectRelease({compatibility:{state}}),false);
  assert.equal(releaseAction({source:{tag:'v2.0.0'}},{tag:'v1.9.0'}),'Switch to older v1.9.0');
  assert.equal(releaseAction({source:{tag:'v1.9.0'}},{tag:'v2.0.0'}),'Update to v2.0.0');
  assert.equal(releaseAction({source:{commit:'abc'}},{commit:'abc',tag:'v2.0.0'}),'Already installed');
});

test('folder import skips repositories and environments, preserves relative source paths and bytes', async () => {
  const file = (path, bytes) => ({name:path.split('/').at(-1), webkitRelativePath:path, size:bytes.length,
    arrayBuffer:async () => Uint8Array.from(bytes).buffer});
  const files = [file('mine/machine.json', [123, 125]), file('mine/vendor/fx.cpp', [0, 128, 255]),
    file('mine/.git/config', [1]), file('mine/node_modules/x.js', [2])];
  assert.equal(sourceFiles(files).length, 2);
  assert.deepEqual(await encodeFolder(files), [{path:'mine/machine.json', data:'e30='}, {path:'mine/vendor/fx.cpp', data:'AID/'}]);
  assert.throws(() => sourceFiles([file('not-a-machine.cpp', [1])]), /machine.json/);
  assert.throws(() => sourceFiles([{...files[0], size:65 * 1024 * 1024}]), /64 MiB/);
});

const identity = Uint8Array.from([0xf0, 0x7e, 0x21, 6, 2, 0, 0x20, 0x76, 0x21, 0, 1, 0, 1, 2, 1, 0, 0xf7]);
test('normal-mode identity must match TE OP-XY exactly', () => {
  assert.equal(isXYIdentity(identity), true);
  for (let at = 0; at < 12; at++) {
    const bad = identity.slice(); bad[at] ^= 1;
    assert.equal(isXYIdentity(bad), false, String(at));
  }
  assert.equal(isXYIdentity(identity.slice(1)), false);
});

test('MTP entry sends identity then the known regular command, without boot-menu handshake', async () => {
  const sent = [], closed = [];
  const input = {name:'OP-XY', state:'connected', open:async () => {}, close:async () => closed.push('input')};
  const output = {name:'OP-XY', state:'connected', open:async () => {}, close:async () => closed.push('output'),
    send:bytes => {
      sent.push(bytes);
      queueMicrotask(() => input.onmidimessage?.({data:sent.length === 1 ? identity :
        Uint8Array.from([0xf0, 0, 0x20, 0x76, 0x21, 0x40, 0x20, 2, 4, 0, 0xf7])}));
    }};
  const access = async options => {
    assert.deepEqual(options, {sysex:true});
    return {inputs:new Map([['input', input]]), outputs:new Map([['output', output]])};
  };
  assert.equal(await enterRegularMTP(access), true);
  assert.deepEqual(sent, [[0xf0, 0x7e, 0x7f, 6, 1, 0xf7], [0xf0, 0, 0x20, 0x76, 0x21, 0x40, 0x60, 2, 4, 0, 1, 2, 0xf7]]);
  assert.deepEqual(closed, ['input', 'output']);
  assert.equal(input.onmidimessage, null);
});

test('already-open MTP sends no MIDI and ambiguous devices are refused', async () => {
  assert.equal(await enterRegularMTP(async () => ({inputs:new Map(), outputs:new Map()})), false);
  const port = {name:'OP-XY', state:'connected'};
  await assert.rejects(enterRegularMTP(async () => ({inputs:new Map([['a', port], ['b', port]]), outputs:new Map([['a', port]])})), /exactly one/);
});

test('a failed MIDI send clears the pending request and releases both ports', async () => {
  const closed = [];
  const input = {name:'OP-XY', state:'connected', open:async () => {}, close:async () => closed.push('in')};
  const output = {name:'OP-XY', state:'connected', open:async () => {}, close:async () => closed.push('out'),
    send:() => { throw new Error('Disconnected'); }};
  await assert.rejects(enterRegularMTP(async () => ({inputs:new Map([['i', input]]), outputs:new Map([['o', output]])})), /Disconnected/);
  assert.equal(input.onmidimessage, null);
  assert.deepEqual(closed, ['in', 'out']);
});

test('connector client pairs with bearer token and never sends arbitrary endpoints', async () => {
  const calls = [];
  const client = new Client('pairing-key', async (url, options) => {
    calls.push({url, options}); return {ok:true, json:async () => ({machines:[]})};
  });
  await client.request('catalog');
  assert.equal(calls[0].url, 'http://127.0.0.1:8920/api/manager/catalog');
  assert.equal(calls[0].options.headers.Authorization, 'Bearer pairing-key');
  assert.equal(calls[0].options.method, 'GET');
  const failed = new Client('bad', async () => ({ok:false, json:async () => ({error:'Pair first'})}));
  await assert.rejects(failed.request('catalog'), /Pair first/);
});

test('already-open regular MTP needs no MIDI permission; only a pre-session mode refusal is retried', async () => {
  let entries = 0, calls = 0;
  const enter = async () => { entries++; };
  assert.equal(await withRegularMTP({run:async () => 'detected'}, 'status', {}, enter), 'detected');
  assert.equal(entries, 0);
  const client = {run:async () => { if (++calls === 1) throw new Error('Open regular COM → M4 MTP'); return 'detected'; }};
  assert.equal(await withRegularMTP(client, 'status', {}, enter), 'detected');
  assert.equal(entries, 1);
  assert.equal(calls, 2);
  for (const message of ['Readback failed', 'Kernel mismatch', 'Device changed', 'Busy']) {
    calls = 0;
    await assert.rejects(withRegularMTP({run:async () => { calls++; throw new Error(message); }}, 'apply', {}, enter), new RegExp(message));
    assert.equal(calls, 1);
    assert.equal(entries, 1);
  }
});
