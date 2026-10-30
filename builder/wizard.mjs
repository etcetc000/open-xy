import * as Ferret from './ferret.mjs';
import * as Mtp from './mtp.mjs';
import * as Visuals from './visuals.mjs';
import {presetFiles, sha256} from './core.mjs';

// Screen state is supplied by the user at the relevant step. Only the explicit
// Enable / Disable actions access MIDI. On a Mac with WebUSB, explicit
// read/send actions copy files over USB and release the device afterwards.
// Otherwise copying is manual. Flashing remains manual.
export function createWizard({getKit, getSelected, hasDownload, getFiles, onFirmware = async () => false, onPreviewExit = () => {}}) {
  const $ = id => document.getElementById(id);
  let purpose = null, stage = null, history = [], revision = 0;
  // Set when the OS-copy guide ends in file transfer mode, so the install guide
  // can continue there; `resumed` marks an install guide that started there.
  let inTransfer = false, resumed = false, transferComputer = null, sourceReadDirectly = false;
  let computer = null, busy = false, versionChecked = false;  // versionChecked: the user confirmed OS 1.2.1
  let hostPage = 1;
  let previewMode = false, previousComputer = null;
  const previewStages = new Set(['boot', 'menu', 'open-dev-menu', 'start-transfer', 'close-field-kit',
    'transfer', 'exit-transfer', 'write-os', 'write-result', 'write-help', 'restart']);
  const sourcePreviewStages = new Set(['boot', 'menu', 'open-dev-menu', 'start-transfer', 'close-field-kit', 'transfer', 'missing-os']);
  // Installing from this page: the chosen device, progress through the build,
  // a log of what happened, and whether every file arrived. `manual` means the user chose to copy.
  let mtp = null, sending = null, log = [], delivered = false, manual = false, downloaded = false;
  // Closing finishes before the device is opened again.
  let closing = Promise.resolve();
  let sourceBackupUrl = null;
  let firmwareState = 'empty';
  function clearSourceBackup() {
    if (sourceBackupUrl) URL.revokeObjectURL(sourceBackupUrl);
    sourceBackupUrl = null; $('source-backup').hidden = true; $('source-backup').removeAttribute('href');
  }
  const stopSending = () => { sending = null; if (mtp) closing = mtp.close(); };
  const release = () => { stopSending(); mtp = null; log = []; delivered = false; };
  const sendsDirectly = () => computer === 'mac' && (previewMode || Mtp.supported()) && !manual;
  const isDisabling = () => ['disable', 'recover'].includes(purpose);
  const direct = () => stage === 'transfer' && ['source', 'install'].includes(purpose) && sendsDirectly();
  const supported = () => globalThis.isSecureContext && !!navigator.requestMIDIAccess;
  const status = (message = '', error = false) => {
    $('dev-status').textContent = message; $('dev-status').dataset.error = String(error);
  };
  const session = new Ferret.Session(options => navigator.requestMIDIAccess(options), () => {
    if (stage && !busy) status('Connection lost. Select the action again to reconnect.', true);
  }, 2500, {
    onProgress: message => { if (stage) status(message); },
    onPorts: ports => {
      $('midi-ports').textContent = Object.entries(ports).map(([direction, list]) =>
        direction.toUpperCase() + '\n' + (list.length ? list.map(port =>
          `${port.name} · ${port.manufacturer} · ${port.state} / ${port.connection}`).join('\n') : '(none)')).join('\n\n');
      $('midi-diagnostics').hidden = false;
    },
  });
  window.addEventListener('pagehide', () => { session.close(); release(); clearSourceBackup(); });
  const action = (label, next, id) => ({label, next, id});
  function content() {
    const install = purpose === 'install', disabling = isDisabling(), recovering = purpose === 'recover', kit = getKit();
    const transfer = computer ? 'open-dev-menu' : 'computer';
    const stillInTransfer = 'Your OP–XY should still be in file transfer mode from copying the OS file. If it isn’t, start from the beginning.';
    const transferSteps = [
      computer === 'mac' ? 'Open Field Kit or OpenMTP.' : 'Open your OP–XY in File Explorer.',
    ];
    if (install) {
      transferSteps.push(downloaded ? 'Extract the ZIP you just downloaded.' : 'Select Download ZIP below, then extract the ZIP.',
        'Drag everything from the extracted ZIP to your OP–XY’s root folder.',
        'Confirm when asked to replace files.');
      transferSteps.push('Wait until every file has finished copying.');
    } else {
      transferSteps.push('Copy OPXY/upgrade/firmware/blackfin/bin/os.ldr to this computer.',
        'If you previously installed open-xy, you can use the OS file, os.ldr, from the device’s top level instead.',
        'Leave your OP–XY in file transfer mode. Installing continues from here.');
    }
    return {
      boot: ['Start in the te-boot menu.', disabling ? 'Finish any file transfers or OS installation before disabling the menu.' : install ? '' : 'The OS file, os.ldr, is your device’s firmware. It is not the .tfw update file on teenage engineering’s site. Follow these steps to copy it.', [action('Connected and at te-boot', 'menu')], [
        'Power off your OP–XY.', 'Hold COM while switching it on.',
        'Release COM when the te-boot menu appears.', 'Connect it to this computer with USB.',
      ]],
      menu: ['Does your screen show "3. dev menu"?', 'Check the te-boot menu on your OP–XY.', [action('No, it’s missing', 'enable', 'menu-no'), action('Yes, I see it', transfer, 'menu-yes')]],
      enable: ['Enable the dev menu.', 'Connect and enable in one step. Allow MIDI access if prompted; this does not change the OS.', [action(busy ? 'Enabling…' : 'Connect and enable dev menu', null, 'dev-enable')], [
        'Keep USB connected and your OP–XY at te-boot. If you restarted after an update, hold COM while switching it on to return there.',
      ]],
      'refresh-open': ['Open the system menu.', 'On your OP–XY, press track 8 for "8. system menu".', [action('System menu is open', 'refresh-return')]],
      'refresh-return': ['Return to te-boot.', 'Press track 8 for "8. back" to refresh the te-boot menu.', [action('I’m back at te-boot', 'confirm')]],
      confirm: ['Does your screen show "3. dev menu"?', 'Check the te-boot menu on your OP–XY.', [action('Yes, I see it', transfer, 'dev-confirm'), action('No, it’s still missing', 'menu-help')]],
      'menu-help': ['The dev menu is still missing.', 'Keep your OP–XY at te-boot and retry enabling it, or use Connection help below.', [action(busy ? 'Enabling…' : 'Retry enabling dev menu', null, 'dev-enable'), action('Refresh the screen again', 'refresh-open')]],
      disabled: recovering ? ['Power cycle and reinstall OS 1.2.1.',
        'The disable command was sent. Reinstall the official OS 1.2.1, even if your OP–XY already reports that version.',
        [action('1.2.1 reinstalled · Try again', null, 'recovery-retry')],
        ['Switch your OP–XY off, then hold COM while switching it back on.',
          'Check that “3. dev menu” is gone. If it is still visible, use Disable dev menu again below before installing.',
          'Download OS 1.2.1 below. Press track 1 for “upload firmware”, then copy the .tfw file to the removable disk and safely eject it.',
          'Wait for the update to finish before returning here. We’ll guide you through enabling the dev menu again to retrieve os.ldr.']]
        : ['Power cycle to finish.', 'The disable command was sent. It does not uninstall your machines or restore the original OS.',
          [action('Dev menu is gone · Done', 'exit'), action(busy ? 'Disabling…' : 'Still visible · Disable again', null, 'dev-disable')],
          ['Switch off, then hold COM while switching on to check that “3. dev menu” is gone.',
            'Once it is gone, restart without holding COM to use your OP–XY.']],
      'missing-os': ['Restore the missing OS file.',
        'A factory reset or initialization can occasionally remove the original os.ldr. Reinstalling OS 1.2.1 with the dev menu disabled restores the file.',
        [action(busy ? 'Disabling…' : 'Disable dev menu', null, 'recovery-disable')],
        ['Wait for any transfer to finish. Close ' + (computer === 'mac' ? 'Field Kit, OpenMTP and Android File Transfer.' : 'any File Explorer windows showing your OP–XY.'),
          'Press track 2 for “2. exit” to leave file transfer mode. Keep USB connected.',
          'Select Disable dev menu and allow MIDI access if prompted. Then we’ll show you how to power cycle and reinstall OS 1.2.1.']],
      computer: ['Which computer are you using?', 'The device has a transfer option for each system.', [action('Windows', 'open-dev-menu', 'computer-windows'), action('Mac', 'open-dev-menu', 'computer-mac')]],
      'open-dev-menu': ['Open the dev menu.', 'Press track 3 for "3. dev menu". Your OP–XY calls this screen "factory menu".', [action('Dev menu is open', 'start-transfer')]],
      'start-transfer': ['Start file transfer.', computer === 'mac' ? 'Press track 2 for "2. mtp file transfer (mac)".' : 'Press track 3 for "3. mtp file transfer (windows)".', [action('I see "mtp mode active"', sendsDirectly() ? 'close-field-kit' : 'transfer')]],
      'close-field-kit': ['Quit Field Kit and OpenMTP.', 'These apps can hold your OP–XY’s USB connection. Quit Field Kit, OpenMTP and Android File Transfer if any are running, then continue.', [action('They’re closed · Continue', 'transfer', 'field-kit-closed')]],
      transfer: !install && sendsDirectly() ? ['Read the original OS from your OP–XY.',
        'Keep your OP–XY in file transfer mode. This reads and verifies the original OS, then loads it into the patcher.',
        [action(busy ? 'Reading…' : log.length ? 'Try again' : 'Read original OS', null, 'mtp-read'),
          ...(busy ? [] : [action('Copy it myself instead', null, 'mtp-manual')])]] : install && sendsDirectly() ? ['Send the files to your OP–XY.',
        resumed ? stillInTransfer : 'Quit OpenMTP or Android File Transfer if either is open. Your device’s other content stays in place.',
        delivered ? [action('Files sent · Continue', 'exit-transfer')]
          : [action(busy ? 'Sending…' : log.length ? 'Try again' : 'Send to OP–XY', null, 'mtp-send'),
            ...(busy ? [] : [action('Copy them myself instead', null, 'mtp-manual')]), ...(resumed ? [action('Start from the beginning', 'boot', 'flow-restart')] : [])]] :
      [install ? 'Copy the ZIP’s files.' : 'Copy the OS file.', resumed ? stillInTransfer : 'Keep your device’s other content in place.', [action(install ? 'All files copied · Continue' : 'Files copied · Choose files', install ? 'exit-transfer' : 'files'), ...(resumed ? [action('Start from the beginning', 'boot', 'flow-restart')] : [])], transferSteps],
      'exit-transfer': ['Exit file transfer.', 'Once every file has finished copying, press track 2 for "2. exit" to return to the dev menu (titled "factory menu" on your OP–XY).', [action('I’m back at the dev menu', 'write-os')]],
      'write-os': ['Write the OS.', 'Press track 5 for "5. write os to boot partition". Keep your OP–XY connected to power.', [action('I pressed track 5', 'write-result')]],
      'write-result': ['Wait for the write result.', 'Don’t power off while writing. When "firmware written ok!" appears under "looking for /os.ldr", it’s safe to power cycle your OP–XY.', [action('I see "firmware written ok!"', 'restart'), action('I see something else', 'write-help')]],
      'write-help': ['Check the message on your OP–XY.', 'If it stops at "looking for /os.ldr" without "firmware written ok!", os.ldr isn’t at the device’s top level. For any other message, stop here and investigate it before power cycling.', [action('Return to the write status', 'write-result')]],
      restart: ['Power cycle your OP–XY.', 'It’s safe now. Switch it off, then on again without holding COM. Then check your selected machines on the device.',
        [action('Device restarted · Done', 'exit'), action(busy ? 'Disabling…' : 'Disable dev menu', null, 'dev-disable')],
        ['Optional: to hide the dev menu, select Disable dev menu before restarting. If you already restarted, hold COM while switching on to return to te-boot first.']],
    }[stage];
  }
  function render(focus = false) {
    if (!stage) return;
    const [title, detail, actions, instructions = []] = content();
    $('device-flow').dataset.stage = stage;
    $('device-flow').setAttribute('aria-busy', String(busy));
    const flowName = purpose === 'recover' ? 'Restore missing OS' : purpose === 'disable' ? 'Disable dev menu' : purpose === 'source' ? 'Copy OS' : 'Install on your OP–XY';
    $('flow-progress').textContent = previewMode ? flowName + ' preview' : flowName + ' · Step ' + (history.length + 1);
    $('flow-preview-note').hidden = !previewMode;
    $('flow-preview-note').textContent = ['source', 'recover'].includes(purpose) ? 'Preview only. Return to the builder to read your original OS.' : 'Preview only. Build firmware in the builder to send files to your OP–XY.';
    $('flow-title').textContent = title;
    const install = purpose === 'install';
    const kit = getKit();
    const files = [install ? 'os.ldr' : 'upgrade/firmware/blackfin/bin/os.ldr'];
    if (install) files.push('content/op1_factory.json');
    if (install) kit?.machines.filter(m => getSelected().includes(m.id) && m.artwork).forEach(m => files.push('content/display/' + m.asset));
    if (install) kit?.machines.filter(m => getSelected().includes(m.id)).forEach(m => files.push(...Object.keys(presetFiles(m))));
    const visual = Visuals.guide(stage, {install, computer, files, disabling: isDisabling()});
    $('flow-visual').replaceChildren(visual);
    $('flow-file-details').hidden = stage !== 'transfer';
    if (focus) $('flow-file-details').open = false;
    $('flow-files').replaceChildren(...(stage === 'transfer' ? [Visuals.fileMap(install, files, install && sendsDirectly())] : []));
    $('flow-detail').textContent = detail; $('flow-detail').hidden = !detail;
    $('flow-instructions').replaceChildren(...instructions.map(text => {
      const li = document.createElement('li'), span = document.createElement('span');
      span.textContent = text; li.append(span); return li;
    }));
    $('flow-instructions').hidden = !instructions.length;
    $('flow-copy-reminder').hidden = !install || stage !== 'transfer' || sendsDirectly();
    $('flow-install-reminder').hidden = !install || !['transfer', 'exit-transfer', 'write-os'].includes(stage);
    $('recovery-downloads').hidden = purpose !== 'recover' || stage !== 'disabled';
    $('flow-missing-os').hidden = purpose !== 'source' || stage !== 'transfer';
    $('missing-os-help').disabled = busy;
    const visibleActions = previewMode && purpose === 'install' && stage === 'transfer'
      ? [...actions, action('Preview next step', 'exit-transfer')] : actions;
    $('flow-actions').replaceChildren(...visibleActions.map((a, i) => {
      const button = document.createElement('button');
      button.type = 'button'; button.textContent = a.label; button.className = i === 0 ? 'primary' : 'secondary';
      if (a.id) button.id = a.id;
      button.disabled = busy || (['dev-enable', 'dev-disable', 'recovery-disable'].includes(a.id) && !supported()) ||
        (previewMode && ['mtp-send', 'mtp-read', 'dev-enable', 'dev-disable', 'recovery-disable'].includes(a.id));
      button.onclick = () => {
        if (['dev-enable', 'dev-disable', 'recovery-disable'].includes(a.id)) return deviceAction(a.id);
        if (a.id === 'recovery-retry') {
          if (busy || purpose !== 'recover' || stage !== 'disabled') return;
          purpose = 'source'; manual = false; history = []; stage = null;
          return advance('enable');
        }
        if (a.id === 'mtp-send') return sendBuild();
        if (a.id === 'mtp-read') return readOriginal();
        if (a.id === 'mtp-manual') { manual = true; sourceReadDirectly = false; release(); status(); return render(true); }
        if (a.id === 'flow-restart') { inTransfer = resumed = sourceReadDirectly = false; history = []; stage = null; }
        if (a.id?.startsWith('computer-')) computer = a.id.slice(9);
        advance(a.next);
      };
      return button;
    }));
    renderDirect();
    $('dev-cancel').hidden = !busy;
    $('dev-cancel').textContent = purpose === 'source' && direct() ? 'Stop reading' : sending ? 'Stop sending' : 'Cancel connection';
    $('flow-back').hidden = !history.length || ['write-os', 'write-result', 'write-help', 'disabled'].includes(stage);
    $('flow-back').disabled = $('flow-exit').disabled = busy;
    $('flow-exit').textContent = previewMode ? 'Back to builder' : hostPage === 1 ? 'Back to file choice' : 'Back to install choice';
    $('flow-download').hidden = previewMode || hostPage !== 4 || !hasDownload() || sendsDirectly();
    $('connection-help').hidden = !['enable', 'menu-help', 'missing-os', 'restart', 'disabled'].includes(stage);
    if (['enable', 'menu-help', 'missing-os', 'restart', 'disabled'].includes(stage) && !supported()) status('MIDI access is unavailable in this browser. Use Connection help below.', true);
    if (focus) { $('flow-title').focus(); Visuals.reveal($('device-flow')); }
  }
  function advance(next) {
    if (busy) return;
    if (next === 'files') { inTransfer = purpose === 'source' && stage === 'transfer'; sourceReadDirectly = false; transferComputer = computer; return showFiles(); }
    if (next === 'write-os') inTransfer = resumed = sourceReadDirectly = false;
    if (next === 'exit') return exit();
    session.close();
    release();
    if (stage) history.push(stage);
    stage = next; status(); render(true);
  }
  function begin(nextPurpose, page = nextPurpose === 'install' ? 4 : 1, preview = null) {
    exit(false); purpose = nextPurpose; hostPage = page; manual = false;
    previousComputer = computer;
    previewMode = !!preview;
    if (preview) { computer = preview.computer; manual = downloaded = false; }
    $('os-version').hidden = $('os-update').hidden = $('computer-choice').hidden = true;
    if (hostPage === 1) $('source-start').hidden = $('file-panel').hidden = $('file-actions').hidden = true;
    else $('download-overview').hidden = $('download-actions').hidden = true;
    $(hostPage === 1 ? 'source-flow-host' : 'install-flow-host').append($('device-flow'));
    $('device-flow').hidden = false;
    // Resume only in the transfer mode the device is already in.
    resumed = !previewMode && purpose === 'install' && inTransfer && !!computer && computer === transferComputer;
    advance(preview?.stage ?? (!resumed ? 'boot' : sendsDirectly() && !sourceReadDirectly ? 'close-field-kit' : 'transfer'));
  }
  function exit(focus = true) {
    const previous = purpose, wasPreview = previewMode;
    previewMode = false;
    if (wasPreview) computer = previousComputer;
    revision++; stage = purpose = null; history = [];
    busy = false; session.close(); release(); status();
    $('device-flow').hidden = true;
    $('download-overview').hidden = $('download-actions').hidden = false;
    if (previous && (hostPage === 1 || wasPreview)) {
      $('source-start').hidden = !computer; $('computer-choice').hidden = !!computer || !versionChecked;
      $('os-version').hidden = !!computer || versionChecked;
    }
    if (focus && wasPreview) { onPreviewExit(); return; }
    if (focus && previous) {
      $(hostPage === 1 ? 'source-title' : 'download-title').focus();
      Visuals.reveal($(hostPage === 1 ? 'source-start' : 'download-overview'));
    }
  }
  function setFirmwareState(state) {
    firmwareState = state;
    const loaded = state === 'loaded', checking = state === 'checking';
    $('firmware-result').dataset.state = state;
    $('firmware-drop').hidden = $('files-note').hidden = loaded || checking;
    $('firmware-replace').hidden = !loaded;
    $('files-title').textContent = loaded ? 'OS file loaded.' : checking ? 'Checking your OS file…' : 'Choose your OS file.';
  }
  // Verified uploads and device reads share a completed state, including on return.
  function showFiles(fromDevice = false) {
    exit(false); $('source-start').hidden = true;
    $('file-panel').hidden = $('file-actions').hidden = false;
    setFirmwareState(fromDevice ? 'loaded' : firmwareState);
    if (fromDevice) $('files-title').textContent = 'Original OS loaded from your OP–XY.';
    (firmwareState === 'empty' ? $('firmware') : $('files-title')).focus();
    Visuals.reveal($('file-panel'));
  }
  async function deviceAction(id) {
    if (previewMode || busy || !supported()) return;
    const enabling = id === 'dev-enable', recovering = id === 'recovery-disable';
    const allowed = enabling ? ['source', 'install'].includes(purpose) && ['enable', 'menu-help'].includes(stage)
      : recovering ? ['source', 'recover'].includes(purpose) && stage === 'missing-os'
      : id === 'dev-disable' && ((purpose === 'install' && stage === 'restart') || (purpose === 'disable' && stage === 'disabled'));
    if (!allowed) return;
    const ticket = ++revision;
    session.close(); release();
    if (recovering) {
      clearSourceBackup(); purpose = 'recover';
      inTransfer = resumed = sourceReadDirectly = false; transferComputer = null; history = [];
    }
    busy = true;
    $('midi-diagnostics').hidden = true; $('midi-ports').textContent = '';
    render(); status(enabling ? 'Connecting to enable the dev menu…' : 'Connecting to disable the dev menu…');
    try {
      await closing;
      if (ticket !== revision) return;
      await session.connect();
      if (ticket !== revision) return;
      status(enabling ? 'Enabling dev menu…' : 'Disabling dev menu…');
      if (enabling) await session.enable(); else await session.disable();
      if (ticket !== revision) return;
      if (!enabling && !recovering) purpose = 'disable';
      busy = false; advance(enabling ? 'refresh-open' : 'disabled');
    } catch (error) {
      if (ticket !== revision) return;
      busy = false; session.close(); render();
      status(error.message || String(error), true);
      $('connection-help').open = true;
    }
  }
  const size = n => n < 1024 ? n + ' bytes' : n < 1024 ** 2 ? (n / 1024).toFixed(1) + ' KiB' : (n / 1024 ** 2).toFixed(1) + ' MiB';
  function showProgress() {
    if (!sending) return;
    const {path, index, count, done, total, fileDone, fileSize, started} = sending;
    const seconds = (performance.now() - started) / 1000;
    $('mtp-bar').style.width = done / total * 100 + '%';
    $('mtp-progress').textContent = `${index + 1} of ${count} files · ${size(done)} of ${size(total)} · ${Math.floor(done / total * 100)}%` +
      (seconds > 0.5 ? ` · ${size(Math.round(done / seconds))}/s` : '');
    $('mtp-current').textContent = `${purpose === 'source' ? 'Reading' : 'Sending'} ${path} · ${size(fileDone)} of ${size(fileSize)}`;
  }
  function renderDirect() {
    $('mtp-title').textContent = purpose === 'source' ? 'Reading from your OP–XY' : 'Sending to your OP–XY';
    $('mtp-panel').hidden = !direct() || (!sending && !log.length);
    $('mtp-sending').hidden = !sending;
    $('mtp-log').hidden = !log.length;
    $('mtp-log').replaceChildren(...log.map(({text, kind}) => {
      const li = document.createElement('li'); li.textContent = text; li.dataset.kind = kind; return li;
    }));
    $('mtp-log').scrollTop = $('mtp-log').scrollHeight;
    showProgress();
  }
  const note = (text, kind = 'info') => { log.push({text, kind}); renderDirect(); };
  // Content first and the OS last, so os.ldr on the device means the copy finished.
  const order = path => path === 'os.ldr' ? 2 : path.startsWith('content/') ? 0 : 1;
  // The chooser needs the click's user activation, so request before any await.
  // The device is open only while sending, so other apps can use it in between.
  async function sendBuild() {
    if (previewMode || busy || !direct()) return;
    const files = Object.entries(getFiles() ?? {}).sort(([a], [b]) => order(a) - order(b) || a.localeCompare(b));
    const total = files.reduce((n, [, bytes]) => n + bytes.length, 0);
    if (!files.length) return status('This build is no longer available. Build it again, then send it.', true);
    const ticket = revision, request = mtp ? Promise.resolve(mtp) : Mtp.Device.request();
    busy = true; log = []; render(); status(mtp ? '' : 'Choose your OP–XY when your browser asks…');
    let device;
    try {
      device = mtp = await request;
      if (ticket !== revision) return;
      status(); note('Connecting to your OP–XY…');
      await closing;
      await device.open();
      // Stopped while connecting: Stop sending couldn't close a device that wasn't open yet.
      if (ticket !== revision) { await device.close(); return; }
      note('Connected. Sending ' + files.length + ' files, ' + size(total) + '.');
      const started = performance.now();
      let before = 0;
      for (const [index, [path, bytes]] of files.entries()) {
        let replaced = false;
        sending = {path, index, count: files.length, done: before, total, fileDone: 0, fileSize: bytes.length, started}; renderDirect();
        await device.send(path, bytes, {
          replace: async () => true,
          onStep: (kind, at) => { if (ticket !== revision) return; if (kind === 'folder') note('Created ' + at + '/'); else replaced = true; },
          onProgress: part => {
            if (ticket !== revision || !sending) return;
            sending.fileDone = Math.round(part * bytes.length); sending.done = before + sending.fileDone; showProgress();
          },
        });
        if (ticket !== revision) return;
        before += bytes.length;
        note(`${replaced ? 'Replaced' : 'Added'} ${path} · ${size(bytes.length)}`, 'done');
      }
      delivered = true;
      note(`Done · ${files.length} files, ${size(total)} in ${((performance.now() - started) / 1000).toFixed(1)} s`, 'done');
    } catch (error) {
      if (ticket !== revision) return;
      // The chooser rejects with NotFoundError when it's dismissed.
      const message = !device && error.name === 'NotFoundError' ? 'No OP–XY chosen.' : error.message || String(error);
      if (log.length) note('Stopped: ' + message + (sending ? ' Select Try again to send every file again.' : ''), 'error');
      status(message, true);
      // The device may have been unplugged; Try again asks the browser for it afresh.
      mtp = null;
    } finally {
      // A cancelled attempt was already closed by Stop sending or leaving the step.
      if (ticket === revision) { closing = device?.close() ?? closing; await closing; busy = false; sending = null; render(); }
    }
  }
  async function readOriginal() {
    if (previewMode || busy || purpose !== 'source' || !direct()) return;
    const kit = getKit();
    if (!kit) return status('The machines have not loaded yet. Reload the page and try again.', true);
    const path = 'upgrade/firmware/blackfin/bin/os.ldr', total = kit.profile.stock_bytes;
    const ticket = revision, request = Mtp.Device.request();
    busy = true; log = []; render(); status('Choose your OP–XY when your browser asks…');
    let device, file;
    try {
      device = await request;
      if (ticket !== revision) return;
      mtp = device;
      await closing;
      await device.open();
      if (ticket !== revision) { await device.close(); return; }
      status(); note('Connected. Reading the original OS…');
      sending = {path, index: 0, count: 1, done: 0, total, fileDone: 0, fileSize: total, started: performance.now()};
      renderDirect();
      const bytes = await device.read(path, {maxBytes: total, onProgress: part => {
        if (ticket !== revision || !sending) return;
        sending.done = sending.fileDone = Math.round(part * total); showProgress();
      }});
      if (ticket !== revision) return;
      note('Checking the original OS…');
      if (bytes.length !== total || await sha256(bytes) !== kit.profile.stock_sha256)
        throw new Error('This is not the supported original OP–XY OS 1.2.1. It has not been loaded.');
      if (ticket !== revision) return;
      file = new File([bytes], 'os.ldr', {type: 'application/octet-stream'});
    } catch (error) {
      if (ticket !== revision) return;
      const message = !device && error.name === 'NotFoundError' ? 'No OP–XY chosen.' : error.message || String(error);
      note(message, 'error'); status(message, true);
    } finally {
      if (ticket === revision) { closing = device?.close() ?? closing; await closing; mtp = null; }
    }
    if (ticket !== revision) return;
    if (file) {
      try {
        if (!await onFirmware(file, () => ticket === revision)) throw new Error('The OS could not be loaded. Try again.');
        if (ticket !== revision) return;
        clearSourceBackup(); sourceBackupUrl = URL.createObjectURL(file);
        $('source-backup').href = sourceBackupUrl; $('source-backup').hidden = false;
        inTransfer = sourceReadDirectly = true; transferComputer = computer;
        busy = false; sending = null; showFiles(true);
        return;
      } catch (error) { if (ticket === revision) { note(error.message, 'error'); status(error.message, true); } }
    }
    if (ticket === revision) { busy = false; sending = null; render(); }
  }
  function renderRoute() {
    $('route-title').textContent = computer ? 'Install from ' + (computer === 'mac' ? 'Mac' : 'Windows') : 'Which computer will you install from?';
    $('route-mac').hidden = computer === 'windows';
    $('download').hidden = computer === 'mac';
    $('route-change').hidden = !computer;
    $('route-mac-note').textContent = Mtp.supported() ? 'Send from this page. No download needed.'
      : 'Direct USB access is unavailable. Download the ZIP and copy it manually.';
  }
  $('dev-cancel').onclick = () => {
    const stopped = !!sending, reading = purpose === 'source' && direct();
    revision++; busy = false; session.close(); stopSending();
    if (reading) { log.push({text: 'Reading stopped. No device files were changed.', kind: 'error'}); render(); status('Select Try again to read the OS again.'); return; }
    if (stopped) log.push({text: 'Stopped by you. Files already sent stay on your OP–XY.', kind: 'error'});
    render(); status(stopped ? 'Sending stopped. Select Try again to send every file again.' : 'Connection cancelled.');
  };
  $('flow-back').onclick = () => {
    if (busy || !history.length) return;
    revision++; session.close(); release();
    stage = history.pop();
    status(); render(true);
  };
  $('flow-exit').onclick = () => exit();
  $('source-local').onclick = () => showFiles();
  $('firmware-replace').onclick = () => {
    if (firmwareState !== 'loaded') return;
    $('firmware').value = ''; $('firmware').click();
  };
  $('source-device').onclick = () => begin('source');
  $('missing-os-help').onclick = () => {
    if (!busy && purpose === 'source' && stage === 'transfer') advance('missing-os');
  };
  $('recovery-disable-again').onclick = () => {
    if (!busy && purpose === 'recover' && stage === 'disabled') advance('missing-os');
  };
  for (const name of ['mac', 'windows']) $('source-' + name).onclick = () => {
    computer = name; manual = false;
    $('computer-choice').hidden = true; $('source-start').hidden = false;
    $('source-title').focus(); Visuals.reveal($('source-start'));
  };
  // The version question comes before everything that reads or loads an OS.
  const showScreen = (from, to, title) => {
    $(from).hidden = true; $(to).hidden = false;
    $(title).focus(); Visuals.reveal($(to));
  };
  $('os-version-yes').onclick = () => { versionChecked = true; showScreen('os-version', 'computer-choice', 'computer-title'); };
  $('os-version-no').onclick = () => showScreen('os-version', 'os-update', 'os-update-title');
  $('os-update-done').onclick = () => { versionChecked = true; showScreen('os-update', 'computer-choice', 'computer-title'); };
  $('os-update-back').onclick = () => showScreen('os-update', 'os-version', 'os-version-title');
  $('source-computer-change').onclick = () => {
    $('source-start').hidden = true; $('computer-choice').hidden = false;
    $('computer-title').focus(); Visuals.reveal($('computer-choice'));
  };
  $('files-back').onclick = () => {
    $('file-panel').hidden = $('file-actions').hidden = true;
    $('source-start').hidden = false; $('source-title').focus();
    Visuals.reveal($('source-start'));
  };
  $('flow-download').addEventListener('click', () => { downloaded = true; });
  // Each route offers only what it needs: a Mac with WebUSB sends from this
  // page; Windows, or a Mac browser without WebUSB, downloads the ZIP.
  function chooseRoute(name, event) {
    computer = name; manual = false;
    if (sendsDirectly()) { event.preventDefault(); if (hasDownload()) begin('install'); return; }
    downloaded = true;
    const url = $('download').getAttribute('href');
    if (event.currentTarget !== $('download')) {
      const link = document.createElement('a'); link.href = url; link.download = 'open-xy.zip'; link.click();
    }
    // Let the anchor start its download before changing the visible step.
    // Browsers do not report whether the user completed or cancelled saving.
    setTimeout(() => {
      if (hasDownload() && $('download').getAttribute('href') === url &&
          !document.querySelector('[data-page="4"]').hidden && !$('download-overview').hidden)
        begin('install');
    }, 0);
  }
  $('route-change').onclick = () => { computer = null; renderRoute(); };
  $('route-mac').onclick = event => chooseRoute('mac', event);
  $('download').onclick = event => chooseRoute('windows', event);
  return {
    clearSourceBackup,
    setFirmwareState,
    previewLink(hash) {
      const match = /^#preview\/(source\/)?(mac|windows)\/([a-z-]+)$/.exec(hash);
      if (busy || !match || !(match[1] ? sourcePreviewStages : previewStages).has(match[3])) return false;
      begin(match[1] ? 'source' : 'install', match[1] ? 1 : 4, {computer: match[2], stage: match[3]});
      return true;
    },
    previewPage() { return previewMode ? hostPage : null; },
    invalidate() { downloaded = false; if (hostPage === 4) exit(false); },
    navigate(page) { if (stage && page !== hostPage) exit(false); if (page === 4) renderRoute(); },
  };
}
