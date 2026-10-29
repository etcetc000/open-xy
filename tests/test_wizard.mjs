import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createWizard} from '../builder/wizard.mjs';
import {Session} from '../builder/ferret.mjs';
import {supportsDeviceApis} from '../builder/browser.mjs';
import * as Visuals from '../builder/visuals.mjs';
import {sha256} from '../builder/core.mjs';
import {fakeDevice} from './fixtures/mtp.mjs';

// Minimal DOM for exercising the real wizard's navigation and device guards.
class Element {
  children = []; dataset = {}; style = {}; hidden = false; textContent = '';
  classList = {add() {}, remove() {}};
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  setAttribute(name, value) { this[name] = value; }
  removeAttribute(name) { delete this[name]; }
  addEventListener() {}
  focus() {}
}

function sourceFlow(t, {fake, bytes, hash, usb = true}) {
  const html = readFileSync(new URL('../builder/index.html', import.meta.url), 'utf8');
  const elements = new Map([...html.matchAll(/<[^>]*\bid="([^"]+)"[^>]*>/g)].map(([tag, id]) =>
    [id, Object.assign(new Element(), {hidden: /\bhidden\b/.test(tag)})]));
  const $ = id => { assert.ok(elements.has(id), id); return elements.get(id); };
  let requests = 0, accepted = [];
  const globals = {
    document: {getElementById: $, createElement: () => new Element(), createElementNS: () => new Element(),
      querySelector: () => new Element()},
    window: {addEventListener() {}}, matchMedia: () => ({matches: true}), isSecureContext: true,
    navigator: {requestMIDIAccess() { throw new Error('Unexpected MIDI access'); },
      usb: usb ? {requestDevice: async () => { requests++; return fake.device; }} : undefined},
  };
  for (const [name, value] of Object.entries(globals)) {
    const previous = Object.getOwnPropertyDescriptor(globalThis, name);
    Object.defineProperty(globalThis, name, {value, configurable: true});
    t.after(() => previous ? Object.defineProperty(globalThis, name, previous) : delete globalThis[name]);
  }
  const wizard = createWizard({getKit: () => ({profile: {stock_bytes: bytes.length, stock_sha256: hash}, machines: []}),
    getSelected: () => [], hasDownload: () => true, getFiles: () => ({}),
    onFirmware: async (file, isCurrent) => { assert.equal(isCurrent(), true); accepted.push(new Uint8Array(await file.arrayBuffer())); return true; }});
  const click = label => {
    const button = $('flow-actions').children.find(b => b.id === label || b.textContent === label);
    assert.ok(button, `Missing action: ${label}`); assert.equal(button.disabled, false); return button.onclick();
  };
  const reachTransfer = computer => {
    $('source-' + computer).onclick(); $('source-device').onclick();
    click('Connected and at te-boot'); click('menu-yes');
    assert.equal($('device-flow').dataset.stage, 'open-dev-menu');
    click('Dev menu is open'); click('I see "mtp mode active"');
  };
  return {$, wizard, click, reachTransfer, accepted, requests: () => requests};
}

test('Mac reads and verifies the original OS, then remembers transfer mode for installation', async t => {
  const bytes = Uint8Array.from({length: 2048}, (_, i) => i * 7);
  const fake = fakeDevice({files: {'upgrade/firmware/blackfin/bin/os.ldr': bytes, 'os.ldr': Uint8Array.of(99)}, split: true});
  const f = sourceFlow(t, {fake, bytes, hash: await sha256(bytes)}), {$, click} = f;
  // The OS version is asked first, before the computer or any OS file.
  assert.equal($('os-version').hidden, false); assert.equal($('computer-choice').hidden, true);
  $('os-version-no').onclick(); assert.equal($('os-update').hidden, false); assert.equal($('os-version').hidden, true);
  $('os-update-done').onclick(); assert.equal($('computer-choice').hidden, false); assert.equal($('os-update').hidden, true);
  assert.equal($('source-start').hidden, true);
  f.reachTransfer('mac');
  assert.equal($('flow-title').textContent, 'Quit Field Kit and OpenMTP.');
  assert.equal(f.requests(), 0); assert.equal($('flow-install-reminder').hidden, true);
  click('field-kit-closed'); await click('mtp-read');
  assert.deepEqual(f.accepted, [bytes]); assert.equal(fake.state.opened, false);
  assert.equal($('file-panel').hidden, false); assert.equal($('device-flow').hidden, true);
  assert.equal($('source-backup').hidden, false); assert.match($('source-backup').href, /^blob:/);
  // Nothing left to choose after a device read: no drop zone, just the result.
  assert.equal($('firmware-drop').hidden, true); assert.equal($('files-title').textContent, 'Original OS loaded from your OP–XY.');
  assert.equal($('firmware-replace').hidden, false);
  assert.equal($('firmware-result').dataset.state, 'loaded');
  $('files-back').onclick(); $('source-local').onclick();
  assert.equal($('firmware-drop').hidden, true);
  assert.equal($('files-title').textContent, 'OS file loaded.');
  assert.deepEqual(new Uint8Array(await (await fetch($('source-backup').href)).arrayBuffer()), bytes);
  f.wizard.clearSourceBackup(); assert.equal($('source-backup').hidden, true);
  assert.ok(!fake.state.ops.some(op => ['100b', '100c', '100d'].includes(op)));
  f.wizard.navigate(4);
  assert.equal($('download').hidden, true); assert.equal($('route-mac').hidden, false);
  $('route-mac').onclick({preventDefault() {}});
  assert.equal($('device-flow').dataset.stage, 'transfer');
  assert.equal($('flow-title').textContent, 'Send the files to your OP–XY.');
  assert.equal($('flow-install-reminder').hidden, false);
});

test('wrong stock hash, missing original and busy apps never load an OS', async t => {
  const bytes = Uint8Array.of(1, 2, 3), hash = await sha256(bytes);
  for (const options of [{files: {'upgrade/firmware/blackfin/bin/os.ldr': Uint8Array.of(4, 5, 6)}},
    {files: {'os.ldr': bytes}}, {busy: true}]) await t.test(JSON.stringify(options), async t => {
    const fake = fakeDevice(options), f = sourceFlow(t, {fake, bytes, hash});
    f.reachTransfer('mac'); f.click('field-kit-closed'); await f.click('mtp-read');
    assert.deepEqual(f.accepted, []); assert.equal(f.$('dev-status').dataset.error, 'true');
    assert.equal(f.$('device-flow').dataset.stage, 'transfer'); assert.equal(fake.state.opened, false);
    assert.ok(!fake.state.ops.some(op => ['100b', '100c', '100d'].includes(op)));
  });
});

test('Windows and browsers without WebUSB keep the manual source path', async t => {
  for (const [computer, usb] of [['windows', true], ['mac', false]]) await t.test(computer, t => {
    const f = sourceFlow(t, {fake: fakeDevice(), bytes: new Uint8Array(1), hash: '', usb});
    f.reachTransfer(computer);
    assert.equal(f.$('device-flow').dataset.stage, 'transfer');
    assert.equal(f.$('flow-title').textContent, 'Copy the OS file.');
    assert.equal(f.requests(), 0); assert.equal(f.$('flow-install-reminder').hidden, true);
  });
});

test('a manual Mac copy still closes transfer apps before sending a build', t => {
  const f = sourceFlow(t, {fake: fakeDevice(), bytes: new Uint8Array(1), hash: ''});
  f.reachTransfer('mac'); f.click('field-kit-closed'); f.click('mtp-manual');
  f.click('Files copied · Choose files');
  f.wizard.navigate(4); f.$('route-mac').onclick({preventDefault() {}});
  assert.equal(f.$('device-flow').dataset.stage, 'close-field-kit');
  assert.equal(f.requests(), 0);
});

test('enabling connects and sends in one click; failures and menu retries keep the intended action', async t => {
  let fail = true; const calls = [];
  for (const [name, replacement] of Object.entries({
    connect: async () => { calls.push('connect'); if (fail) throw new Error('Not connected'); },
    enable: async () => calls.push('enable'),
    disable: async () => { throw new Error('Must not disable'); },
  })) {
    const original = Session.prototype[name]; Session.prototype[name] = replacement;
    t.after(() => { Session.prototype[name] = original; });
  }
  const f = sourceFlow(t, {fake: fakeDevice(), bytes: new Uint8Array(1), hash: ''});
  f.$('source-windows').onclick(); f.$('source-device').onclick();
  f.click('Connected and at te-boot'); f.click('menu-no');
  assert.deepEqual(calls, []);
  await f.click('dev-enable');
  assert.equal(f.$('device-flow').dataset.stage, 'enable');
  assert.equal(f.$('connection-help').open, true);
  assert.deepEqual(calls, ['connect']);
  fail = false; await f.click('dev-enable');
  assert.equal(f.$('device-flow').dataset.stage, 'refresh-open');
  assert.match(f.$('flow-detail').textContent, /track 8.*system menu/);
  f.click('System menu is open');
  assert.equal(f.$('device-flow').dataset.stage, 'refresh-return');
  assert.match(f.$('flow-detail').textContent, /track 8.*back/);
  f.click('I’m back at te-boot');
  assert.equal(f.$('device-flow').dataset.stage, 'confirm');
  assert.deepEqual(calls, ['connect', 'connect', 'enable']);
  f.click('No, it’s still missing');
  f.click('Refresh the screen again');
  assert.equal(f.$('device-flow').dataset.stage, 'refresh-open');
  f.click('System menu is open'); f.click('I’m back at te-boot');
  f.click('No, it’s still missing');
  await f.click('dev-enable');
  assert.equal(f.$('device-flow').dataset.stage, 'refresh-open');
  f.click('System menu is open'); f.click('I’m back at te-boot');
  assert.equal(f.$('device-flow').dataset.stage, 'confirm');
  assert.deepEqual(calls.slice(-2), ['connect', 'enable']);
  f.click('dev-confirm');
  assert.equal(f.$('device-flow').dataset.stage, 'open-dev-menu');
});

test('post-install disable runs directly and retries without restarting setup', async t => {
  let fail = true; const calls = [];
  for (const [name, replacement] of Object.entries({
    connect: async () => calls.push('connect'),
    disable: async () => { calls.push('disable'); if (fail) throw new Error('Disable failed'); },
    enable: async () => { throw new Error('Must not enable'); },
  })) {
    const original = Session.prototype[name]; Session.prototype[name] = replacement;
    t.after(() => { Session.prototype[name] = original; });
  }
  const f = sourceFlow(t, {fake: fakeDevice(), bytes: new Uint8Array(1), hash: ''});
  f.$('source-mac').onclick(); f.wizard.navigate(4);
  f.$('route-mac').onclick({preventDefault() {}});
  f.click('Connected and at te-boot'); f.click('menu-yes'); f.click('Dev menu is open');
  f.click('I see "mtp mode active"'); f.click('field-kit-closed'); f.click('mtp-manual');
  f.click('All files copied · Continue');
  assert.equal(f.$('device-flow').dataset.stage, 'exit-transfer');
  assert.match(f.$('flow-detail').textContent, /track 2/);
  f.click('I’m back at the dev menu');
  assert.equal(f.$('device-flow').dataset.stage, 'write-os');
  assert.match(f.$('flow-detail').textContent, /track 5/);
  f.click('I pressed track 5');
  assert.equal(f.$('device-flow').dataset.stage, 'write-result');
  f.click('I see "firmware written ok!"');
  assert.equal(f.$('device-flow').dataset.stage, 'restart');
  assert.deepEqual(calls, []);
  await f.click('dev-disable');
  assert.equal(f.$('device-flow').dataset.stage, 'restart');
  assert.equal(f.$('connection-help').open, true);
  assert.deepEqual(calls, ['connect', 'disable']);
  fail = false; await f.click('dev-disable');
  assert.equal(f.$('device-flow').dataset.stage, 'disabled');
  assert.equal(f.$('flow-title').textContent, 'Power cycle to finish.');
  assert.equal(f.$('flow-back').hidden, true);
  assert.equal(f.$('recovery-downloads').hidden, true);
  await f.click('dev-disable');
  assert.deepEqual(calls, ['connect', 'disable', 'connect', 'disable', 'connect', 'disable']);
  f.click('Dev menu is gone · Done');
  assert.equal(f.$('device-flow').hidden, true);
  assert.equal(f.requests(), 0);
});

test('missing OS recovery disables the menu before reinstalling, then restarts the correct source route', async t => {
  for (const computer of ['mac', 'windows']) await t.test(computer, async t => {
    let connects = 0, disables = 0, enables = 0;
    for (const [name, replacement] of Object.entries({connect: async () => connects++, disable: async () => disables++,
      enable: async () => enables++})) {
      const original = Session.prototype[name]; Session.prototype[name] = replacement;
      t.after(() => { Session.prototype[name] = original; });
    }
    const f = sourceFlow(t, {fake: fakeDevice(), bytes: new Uint8Array(1), hash: ''}), {$, click} = f;
    f.reachTransfer(computer);
    if (computer === 'mac') {
      assert.equal($('flow-missing-os').hidden, true);
      click('field-kit-closed'); click('mtp-manual');
    }
    assert.equal($('flow-missing-os').hidden, false);
    assert.equal($('flow-install-reminder').hidden, true);
    assert.ok($('flow-instructions').children.some(li => li.children[0].textContent.includes('OPXY/upgrade/firmware/blackfin/bin/os.ldr')));
    $('missing-os-help').onclick();
    assert.equal($('device-flow').dataset.stage, 'missing-os');
    assert.equal(f.requests(), 0); assert.equal(connects, 0); assert.equal(disables, 0);
    assert.equal($('flow-missing-os').hidden, true);
    assert.equal($('recovery-downloads').hidden, true);
    await click('recovery-disable');
    assert.equal(connects, 1); assert.equal(disables, 1);
    assert.equal($('flow-title').textContent, 'Power cycle and reinstall OS 1.2.1.');
    assert.equal($('recovery-downloads').hidden, false);
    assert.equal($('flow-back').hidden, true);
    assert.ok($('flow-instructions').children.some(li => li.children[0].textContent.includes('hold COM')));
    $('recovery-disable-again').onclick();
    assert.equal($('device-flow').dataset.stage, 'missing-os');
    assert.equal($('recovery-downloads').hidden, true);
    await click('recovery-disable');
    assert.equal(connects, 2); assert.equal(disables, 2);
    assert.equal(enables, 0);
    click('recovery-retry');
    assert.equal($('device-flow').dataset.stage, 'enable');
    assert.equal(enables, 0); // A new explicit click is still required after reinstalling.
    await click('dev-enable');
    assert.equal(enables, 1); assert.equal(connects, 3);
    assert.equal($('device-flow').dataset.stage, 'refresh-open');
    click('System menu is open'); click('I’m back at te-boot');
    assert.equal($('device-flow').dataset.stage, 'confirm');
    // Retry retains the computer and goes straight to enabling, without repeating setup questions.
    click('dev-confirm'); click('Dev menu is open');
    assert.match($('flow-detail').textContent, computer === 'mac' ? /track 2/ : /track 3/);
    click('I see "mtp mode active"');
    assert.equal($('device-flow').dataset.stage, computer === 'mac' ? 'close-field-kit' : 'transfer');
  });
});

test('recovery connection and disable failures stay on the same step for retry', async t => {
  for (const failure of ['connect', 'disable']) await t.test(failure, async t => {
    let fails = true, disables = 0;
    for (const [name, replacement] of Object.entries({
      connect: async () => { if (fails && failure === 'connect') throw new Error('Connection failed'); },
      disable: async () => { disables++; if (fails && failure === 'disable') throw new Error('Disable failed'); },
    })) {
      const original = Session.prototype[name]; Session.prototype[name] = replacement;
      t.after(() => { Session.prototype[name] = original; });
    }
    const f = sourceFlow(t, {fake: fakeDevice(), bytes: new Uint8Array(1), hash: ''});
    f.reachTransfer('windows'); f.$('missing-os-help').onclick();
    await f.click('recovery-disable');
    assert.equal(f.$('device-flow').dataset.stage, 'missing-os');
    assert.equal(f.$('recovery-downloads').hidden, true);
    assert.equal(f.$('connection-help').open, true);
    assert.match(f.$('dev-status').textContent, /failed/);
    assert.equal(disables, failure === 'connect' ? 0 : 1);
    fails = false;
    await f.click('recovery-disable');
    assert.equal(f.$('device-flow').dataset.stage, 'disabled');
  });
});

test('cancelling recovery during connection or disabling cannot advance or send a later command', async t => {
  for (const pending of ['connect', 'disable']) await t.test(pending, async t => {
    let finish, started, disables = 0;
    const reached = new Promise(resolve => { started = resolve; });
    const blocked = () => { started(); return new Promise(resolve => { finish = resolve; }); };
    for (const [name, replacement] of Object.entries({
      connect: async () => { if (pending === 'connect') await blocked(); },
      disable: async () => { disables++; if (pending === 'disable') await blocked(); },
    })) {
      const original = Session.prototype[name]; Session.prototype[name] = replacement;
      t.after(() => { Session.prototype[name] = original; });
    }
    const f = sourceFlow(t, {fake: fakeDevice(), bytes: new Uint8Array(1), hash: ''});
    f.reachTransfer('windows'); f.$('missing-os-help').onclick();
    const action = f.$('flow-actions').children.find(b => b.id === 'recovery-disable');
    const running = action.onclick(); await reached;
    assert.equal(f.$('flow-actions').children[0].disabled, true);
    await action.onclick(); // Even a stale handler cannot start a second operation.
    f.$('dev-cancel').onclick(); finish(); await running;
    assert.equal(disables, pending === 'connect' ? 0 : 1);
    assert.equal(f.$('device-flow').dataset.stage, 'missing-os');
    assert.equal(f.$('recovery-downloads').hidden, true);
    assert.equal(f.$('flow-actions').children[0].disabled, false);
  });
});

test('stopping a Mac read discards it and releases USB before a retry', async t => {
  const bytes = new Uint8Array(32), fake = fakeDevice({files: {'upgrade/firmware/blackfin/bin/os.ldr': bytes}, hang: 0x1009});
  const f = sourceFlow(t, {fake, bytes, hash: await sha256(bytes)});
  f.reachTransfer('mac'); f.click('field-kit-closed'); const reading = f.click('mtp-read');
  while (!fake.state.abort) await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(f.$('missing-os-help').disabled, true);
  f.$('missing-os-help').onclick();
  assert.equal(f.$('device-flow').dataset.stage, 'transfer');
  f.$('dev-cancel').onclick(); await reading;
  assert.deepEqual(f.accepted, []); assert.equal(fake.state.opened, false);
  assert.equal(f.$('dev-cancel').hidden, true);
});

test('installation links preview the edited steps without a build or device access', async t => {
  const html = readFileSync(new URL('../builder/index.html', import.meta.url), 'utf8');
  const elements = new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(([, id]) => [id, new Element()]));
  const $ = id => { assert.ok(elements.has(id), `Missing authored element: ${id}`); return elements.get(id); };
  let deviceRequests = 0, exits = 0;
  const listeners = new Map();
  const pages = [1, 2, 3, 4].map(page => Object.assign(new Element(), {dataset: {page: String(page)}}));
  const steps = [1, 2, 3, 4].map(step => Object.assign(new Element(), {dataset: {step: String(step)}}));
  const location = {hash: '#preview/mac/transfer', pathname: '/', search: ''};
  const globals = {
    document: {
      getElementById: $, createElement: () => new Element(), createElementNS: () => new Element(),
      addEventListener() {},
      querySelectorAll: selector => ({'[data-icon]': [], '[data-page]': pages, '.steps button': steps}[selector] ?? []),
      querySelector: selector => {
        const match = /^\[data-page="([1234])"\]( h1)?$/.exec(selector);
        assert.ok(match, `Unexpected selector: ${selector}`);
        return match[2] ? $(['input-title', 'machines-title', 'extensions-title', 'download-title'][Number(match[1]) - 1]) : pages[Number(match[1]) - 1];
      },
    },
    window: {addEventListener: (event, listener) => listeners.set(event, listener)},
    location,
    history: {replaceState() { location.hash = ''; }},
    matchMedia: () => ({matches: true}),
    isSecureContext: true,
    navigator: {
      usb: {requestDevice: () => { deviceRequests++; throw new Error('Unexpected USB access'); }},
      requestMIDIAccess: () => { deviceRequests++; throw new Error('Unexpected MIDI access'); },
    },
  };
  for (const [name, value] of Object.entries(globals)) {
    const descriptor = Object.getOwnPropertyDescriptor(globalThis, name);
    Object.defineProperty(globalThis, name, {value, configurable: true});
    t.after(() => descriptor ? Object.defineProperty(globalThis, name, descriptor) : delete globalThis[name]);
  }
  const wizard = createWizard({getKit: () => null, getSelected: () => [], hasDownload: () => false,
    getFiles: () => ({'os.ldr': new Uint8Array(1)}), onPreviewExit: () => exits++});
  const action = label => {
    const button = $('flow-actions').children.find(button => button.textContent === label || button.id === label);
    assert.ok(button, `Missing action: ${label}`); return button;
  };

  for (const hash of ['#preview/mac/transfer', '#preview/windows/transfer']) {
    assert.equal(wizard.previewLink(hash), true);
    assert.equal($('flow-install-reminder').hidden, false);
    assert.equal($('flow-preview-note').hidden, false);
    assert.equal($('flow-download').hidden, true);
    assert.equal($('flow-missing-os').hidden, true);
  }
  wizard.previewLink('#preview/mac/transfer');
  assert.equal($('flow-title').textContent, 'Send the files to your OP–XY.');
  assert.equal(action('mtp-send').disabled, true);
  await action('mtp-send').onclick(); // Also guard the handler, independently of the disabled button.
  assert.equal(deviceRequests, 0);
  action('Preview next step').onclick();
  assert.equal($('device-flow').dataset.stage, 'exit-transfer');
  assert.equal($('flow-install-reminder').hidden, false);
  action('I’m back at the dev menu').onclick();
  assert.equal($('device-flow').dataset.stage, 'write-os');
  assert.equal($('flow-install-reminder').hidden, false);
  action('I pressed track 5').onclick();
  assert.equal($('flow-install-reminder').hidden, true);

  for (const stage of ['boot', 'start-transfer', 'close-field-kit']) {
    assert.equal(wizard.previewLink('#preview/mac/' + stage), true);
    assert.equal($('flow-install-reminder').hidden, true);
  }
  assert.equal(wizard.previewLink('#preview/source/mac/transfer'), true);
  assert.equal($('flow-missing-os').hidden, false);
  assert.equal(wizard.previewPage(), 1);
  assert.equal($('flow-install-reminder').hidden, true);
  assert.equal(action('mtp-read').disabled, true);
  await action('mtp-read').onclick();
  assert.equal(deviceRequests, 0);
  assert.equal(wizard.previewLink('#preview/source/windows/missing-os'), true);
  assert.equal(action('recovery-disable').disabled, true);
  await action('recovery-disable').onclick();
  assert.equal($('device-flow').dataset.stage, 'missing-os');
  assert.equal($('recovery-downloads').hidden, true);
  assert.equal(deviceRequests, 0);
  wizard.previewLink('#preview/mac/menu');
  action('menu-no').onclick();
  assert.equal(action('dev-enable').disabled, true);
  await action('dev-enable').onclick();
  assert.equal(deviceRequests, 0);
  for (const hash of ['#preview/mac/enable', '#preview/linux/transfer', '#preview/mac/transfer/extra']) {
    assert.equal(wizard.previewLink(hash), false);
    assert.equal($('device-flow').dataset.stage, 'enable');
  }
  wizard.previewLink('#preview/mac/restart');
  assert.equal(action('dev-disable').disabled, true);
  await action('dev-disable').onclick();
  assert.equal($('device-flow').dataset.stage, 'restart');
  assert.equal(deviceRequests, 0);
  $('flow-exit').onclick();
  assert.equal(exits, 1);
  assert.equal($('device-flow').hidden, true);
  $('source-device').onclick();
  assert.equal($('flow-preview-note').hidden, true);
  assert.equal($('flow-install-reminder').hidden, true);

  // Run the actual app startup against the same DOM. It must render a valid
  // preview before waiting for acknowledgement, without booting the builder.
  const source = readFileSync(new URL('../builder/app.mjs', import.meta.url), 'utf8').replace(/^import .*\r?\n/gm, '');
  const AsyncFunction = Object.getPrototypeOf(async function() {}).constructor;
  let startupError;
  navigator.usb = undefined; // Unsupported browser; previews must still work.
  new AsyncFunction('Wizard', 'Visuals', 'supportsDeviceApis', source)({createWizard}, Visuals, supportsDeviceApis).catch(error => { startupError = error; });
  await Promise.resolve();
  assert.ifError(startupError);
  assert.equal($('risk-gate').hidden, true);
  assert.equal($('risk-acknowledgement').checked, false);
  assert.equal($('risk-acknowledgement').disabled, true);
  assert.equal($('builder-workspace').inert, false);
  assert.equal($('device-flow').dataset.stage, 'transfer');
  assert.equal(action('mtp-send').disabled, true);
  $('flow-exit').onclick();
  assert.equal($('browser-gate').hidden, false);
  assert.equal($('risk-gate').hidden, true);
  assert.equal($('builder-workspace').inert, true);
  assert.equal($('risk-acknowledgement').checked, false);
  // Unsupported browsers cannot enter the builder even if a checkbox
  // change or submit handler is invoked directly after leaving a preview.
  $('risk-acknowledgement').checked = true;
  $('risk-acknowledgement').onchange();
  assert.equal($('risk-continue').disabled, true);
  $('risk-form').onsubmit({preventDefault() {}});
  assert.equal($('builder-workspace').inert, true);
  assert.equal($('browser-gate').hidden, false);
  assert.equal($('risk-gate').hidden, true);
  location.hash = '#preview/mac/write-os';
  listeners.get('hashchange')();
  assert.equal($('risk-gate').hidden, true);
  assert.equal($('device-flow').dataset.stage, 'write-os');
  location.hash = '#preview/mac/not-a-step';
  listeners.get('hashchange')();
  assert.equal($('browser-gate').hidden, false);
  assert.equal($('risk-gate').hidden, true);
  assert.equal($('builder-workspace').inert, true);
  assert.equal(deviceRequests, 0);
  location.hash = '#preview/source/mac/transfer';
  listeners.get('hashchange')();
  assert.equal($('device-flow').dataset.stage, 'transfer');
  assert.equal($('risk-gate').hidden, true);
  assert.equal(action('mtp-read').disabled, true);
  assert.equal(pages[0].hidden, false);
});
