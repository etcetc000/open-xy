import {createGraphicsConfiguration} from './graphics-configuration.mjs';
import {createSnapshotService} from './machine-snapshots.mjs';
import {createGraphicsDialog} from './graphics-dialog.mjs';
import * as Core from './core.mjs';
import * as Wizard from './wizard.mjs';
import * as Retained from '../sdk/browser/display.mjs';
import * as Visuals from './visuals.mjs';
import {editedShown, shownControls} from '../sdk/browser/controls.mjs';
import {supportsDeviceApis} from './browser.mjs';

// The entry point supplies audition's bundled worker sources; the controller
// can also be mounted with explicit adapters by firmware-free integration tests.
export async function createBuilder({core = Core, wizardApi = Wizard, retained = Retained, visuals = Visuals,
  audition, supportsBrowser = supportsDeviceApis} = {}) {
  const $ = id => document.getElementById(id);
  document.querySelectorAll('[data-icon]').forEach(el => el.append(visuals.icon(el.dataset.icon)));
  document.querySelectorAll('[data-device-art]').forEach(el => el.append(visuals.device()));
  // Preview links can show the guide immediately; using the builder still needs acknowledgement.
  let acknowledged = false;
  let builderPreview = false, loadCollection;
  const browserAllowed = supportsBrowser();
  $('browser-blocked').hidden = browserAllowed;
  $('risk-acknowledgement').checked = false;
  $('risk-acknowledgement').disabled = !browserAllowed;
  $('risk-continue').disabled = true;
  const colors = ['var(--machine-1)', 'var(--machine-2)', 'var(--machine-3)', 'var(--machine-4)'];
  const kib = n => (n / 1024).toFixed(1) + ' KiB', mib = n => n / 1024 ** 2 + ' MiB';
  const cpuWarning = 'Uses more CPU than usual. This may reduce the total number of voices you can play at once.';
  let cpuTooltip;
  function cpuBadge(badge, high) {
    badge.removeAttribute('title');
    badge.replaceChildren('CPU-high', ...(high ? [node('span', ': ' + cpuWarning, 'sr-only')] : []));
    const show = () => {
      if (!high) return;
      if (!cpuTooltip) {
        cpuTooltip = document.createElement('div');
        cpuTooltip.className = 'cpu-high-tooltip';
        cpuTooltip.setAttribute('role', 'tooltip');
        cpuTooltip.textContent = cpuWarning; document.body.append(cpuTooltip);
      }
      cpuTooltip.hidden = false;
      const box = badge.getBoundingClientRect(), tip = cpuTooltip.getBoundingClientRect();
      cpuTooltip.style.left = Math.max(8, Math.min(box.left, window.innerWidth - tip.width - 8)) + 'px';
      cpuTooltip.style.top = (box.bottom + tip.height + 8 < window.innerHeight
        ? box.bottom + 6 : Math.max(8, box.top - tip.height - 6)) + 'px';
    };
    const hide = () => { if (cpuTooltip) cpuTooltip.hidden = true; };
    badge.onmouseenter = show; badge.onmouseleave = hide;
    const focusTarget = badge.closest('button') ?? badge;
    if (focusTarget === badge) badge.tabIndex = high ? 0 : -1;
    focusTarget.onfocus = show; focusTarget.onblur = hide;
    focusTarget.onkeydown = event => { if (event.key === 'Escape') hide(); };
  }
  document.addEventListener('scroll', () => { if (cpuTooltip) cpuTooltip.hidden = true; }, true);

  const graphicsConfiguration = createGraphicsConfiguration();
  // Experimental 4 MiB module memory (core.memoryKit), chosen on the machines page.
  // Off for every collection until chosen; restoring a 4 MiB build or its layout
  // turns it on. The checkbox is the state; expandedKit is derived once per
  // collection, null when not offered.
  let expandedKit = null;
  const memoryInput = $('expanded-memory');
  memoryInput.checked = false; memoryInput.disabled = true;
  const expandedMemory = () => memoryInput.checked;
  const kitFor = expanded => expanded ? expandedKit : kit, chosenKit = () => kitFor(expandedMemory());
  const expandedSize = () => mib(expandedKit.profile.reserved_bytes);
  // A does-not-fit message, pointing to the larger option when this site offers it and
  // the selection would fit it (any failure there means no hint).
  // Only after a selection fails to fit: one more in-browser link, under a millisecond.
  const fitsLarger = (selection, layoutNumbers, extensions) => {
    try { return Boolean(expandedKit && core.linkKit(expandedKit, selection, layoutNumbers, extensions)); } catch { return false; }
  };
  // No hint while an OS is being checked: it sets the option itself.
  const roomHint = (message, then, ...selection) => (/[.!?]$/.test(message) ? message : message + '.') +
    (!checkingOs && fitsLarger(...selection) ? ` Turn on “Use ${expandedSize()} for machines” (experimental) ${then}.` : '');
  let kit, stock, catalog, selected = [], selectedExtensions = [], inspected, inspectedExtension, linked, downloadUrl, built, layout, revision = 0;
  // The machine numbers of this person's firmware (core.mjs assignNumbers): from their
  // last build here, or a layout.json they load. Kept in this browser as a convenience;
  // the layout.json in every download is the record.
  const NUMBERS_KEY = 'open-xy-machine-numbers';
  let numbers = null;
  function rememberNumbers(value) {
    numbers = value;
    try { localStorage.setItem(NUMBERS_KEY, JSON.stringify({profile: kit.profile.id, machines: value})); } catch {}
  }
  // How the machine list is searched, filtered and sorted (core.browseMachines).
  // Favorites are kept in this browser only, by machine ID.
  const FAVORITES_KEY = 'open-xy-favorite-machines';
  const browsing = {query: '', kind: 'all', keywords: [], favoritesOnly: false, sort: 'featured'};
  let favorites = new Set();
  try {
    const stored = JSON.parse(localStorage.getItem(FAVORITES_KEY) ?? '[]');
    if (Array.isArray(stored)) favorites = new Set(stored.filter(id => typeof id === 'string'));
  } catch {}
  function toggleFavorite(id) {
    if (!favorites.delete(id)) favorites.add(id);
    try { localStorage.setItem(FAVORITES_KEY, JSON.stringify([...favorites])); } catch {}
  }
  // Named machine selections, kept in this browser only (core.readPresets). A preset
  // chooses machines; their numbers still come from the layout.
  const PRESETS_KEY = 'open-xy-machine-presets';
  let presets = [];
  try { presets = core.readPresets(JSON.parse(localStorage.getItem(PRESETS_KEY) ?? '[]')); } catch {}
  function storePresets(next) {
    presets = next;
    try { localStorage.setItem(PRESETS_KEY, JSON.stringify(presets)); }
    catch { $('preset-message').textContent = 'This browser could not keep the preset; it lasts until you leave the page.'; }
    renderPresets();
  }
  let stopPreview = () => {}, previewRevision = 0;
  let stopAudio = () => {};
  window.addEventListener('blur', () => stopAudio());
  window.addEventListener('pagehide', () => stopPreview());
  document.addEventListener('visibilitychange', () => { if (document.hidden) stopAudio(); });
  document.addEventListener('keydown', event => { if (event.key === 'Escape') stopAudio(); });
  const status = (message = '', error = false) => { $('status').textContent = message; $('status').dataset.error = String(error); };
  const fail = error => status(error.message || String(error), true);
  function invalidate() {
    wizard.invalidate();
    revision++; layout = built = null;
    if (downloadUrl) { URL.revokeObjectURL(downloadUrl); downloadUrl = null; }
    $('download').removeAttribute('href');
    $('flow-download').removeAttribute('href');
    document.querySelector('[data-step="4"]').disabled = true;
  }
  function navigate(step) {
    stopAudio();
    if ($('selected-previews').open) $('selected-previews').close();
    if (builderPreview && ![2, 3].includes(step)) {
      builderPreview = false;
      history.replaceState(null, '', location.pathname + location.search);
      ready();
      if (!acknowledged) { showEntryGate(); return; }
    }
    wizard.navigate(step);
    if ([2, 3].includes(step)) $(step === 2 ? 'machine-capacity-host' : 'extension-capacity-host').append($('selection-capacity'));
    document.querySelectorAll('[data-page]').forEach(p => p.hidden = Number(p.dataset.page) !== step);
    document.querySelectorAll('.steps button').forEach(b => {
      if (Number(b.dataset.step) === step) b.setAttribute('aria-current', 'step'); else b.removeAttribute('aria-current');
    });
    document.querySelector(`[data-page="${step}"] h1`).focus();
    visuals.reveal(document.querySelector(`[data-page="${step}"]`));
  }
  function ready() {
    $('choose').disabled = !(kit && stock);
    document.querySelector('.steps [data-step="2"]').disabled = !(kit && (stock || builderPreview));
    $('choose-extensions').disabled = !(kit && selected.length && (stock || builderPreview));
    document.querySelector('.steps [data-step="3"]').disabled = $('choose-extensions').disabled;
    $('build').disabled = builderPreview || !(stock && linked);
    $('preview-selected').disabled = !(kit && selected.length);
  }

  const wizard = wizardApi.createWizard({ getKit: () => kit, getSelected: () => selected, hasDownload: () => !!downloadUrl, getFiles: () => built,
    onFirmware: (file, isCurrent) => acceptFirmware(file, isCurrent),
    onPreviewExit() {
      history.replaceState(null, '', location.pathname + location.search);
      if (acknowledged) navigate(1); else showEntryGate();
    },
  });
  function showEntryGate() {
    stopPreview();
    ++previewRevision;
    wizard.navigate(1);
    $('browser-gate').hidden = browserAllowed;
    $('risk-gate').hidden = !browserAllowed;
    $('phase-nav').hidden = true;
    $('builder-workspace').hidden = true;
    $('builder-workspace').inert = true;
  }
  function openPreviewLink() {
    const match = /^#preview\/(machines|extensions|settings)(?:\/([a-z0-9_-]+))?$/.exec(location.hash);
    const leavingMachines = builderPreview && !match;
    builderPreview = !!match;
    if (builderPreview || wizard.previewLink(location.hash)) {
      $('browser-gate').hidden = true;
      $('risk-gate').hidden = true;
      $('builder-workspace').hidden = false;
      $('builder-workspace').inert = false;
      if (!acknowledged) $('status').textContent = '';
      if (builderPreview) {
        loadCollection?.();
        $('phase-nav').hidden = false;
        navigate(match[1] === 'machines' ? 2 : 3);
        if (kit) {
          if (match[1] !== 'machines') inspectExtension(kit.extensions?.find(e => e.id === match[2])?.id ?? kit.extensions?.[0]?.id);
          else inspect(kit.machines.find(m => m.id === match[2])?.id ?? kit.machines[0].id);
          recalculate();
        }
      } else {
        $('phase-nav').hidden = !acknowledged;
        navigate(wizard.previewPage());
      }
    } else if (!acknowledged) showEntryGate();
    else if (leavingMachines) navigate(1);
    if (builderPreview || leavingMachines) ready();
  }
  window.addEventListener('hashchange', openPreviewLink);
  openPreviewLink();
  // Machine previews may load the collection without acknowledging the notice.
  // Leaving a preview still returns to the gate; installing requires acknowledgement.
  await new Promise(resolve => {
    loadCollection = resolve;
    if (builderPreview) resolve();
    $('risk-acknowledgement').onchange = () => {
      $('risk-continue').disabled = !browserAllowed || !$('risk-acknowledgement').checked;
    };
    $('risk-form').onsubmit = event => {
      event.preventDefault();
      if (!browserAllowed || !$('risk-acknowledgement').checked || !$('risk-form').reportValidity()) return;
      acknowledged = true;
      $('risk-gate').hidden = true;
      $('phase-nav').hidden = false;
      $('builder-workspace').hidden = false;
      $('builder-workspace').inert = false;
      navigate(1);
      resolve();
    };
  });

  async function readBounded(file, limit) {
    core.requireThat(file && file.size > 0 && file.size <= limit, 'Missing or oversized file');
    return new Uint8Array(await file.arrayBuffer());
  }
  // These authored examples use the same paint conversion as packaged artwork.
  const graphicsExamples = visuals.graphicsExamples(core.artworkForGraphics);
  $('graphics-examples').replaceChildren(...graphicsExamples.elements);
  $('preview-controls-panel').ontoggle = () => {
    if (!$('preview-controls-panel').open) stopAudio();
  };
  const selectedMachines = () => kit?.machines.filter(machine => selected.includes(machine.id)) ?? [];
  function updateOverrideCount() {
    const {enabled:grayscale, customized:count} = graphicsConfiguration.summary(selectedMachines());
    $('grayscale-graphics').checked = grayscale;
    graphicsExamples.setGraphics(grayscale ? 'grayscale' : 'colorful');
    $('graphics-overrides-count').textContent = count ? `${count} machine${count === 1 ? '' : 's'} customized` : 'All machines use the default.';
    $('graphics-extension').className = 'machine graphics-extension' + (grayscale ? ' selected' : '');
    if (!kit) return;
    const enabled = selectedExtensions.length + Number(grayscale);
    $('extension-count').textContent = enabled ? enabled + ' selected' : 'None selected';
    facts($('build-selection'), {
      Machines: selectedMachines().map(m => m.name).join(', ') || 'None',
      Extensions: (kit.extensions ?? []).filter(e => selectedExtensions.includes(e.id)).map(e => e.name).join(', ') || 'None',
      ...(expandedKit ? {Memory: expandedMemory() ? expandedSize() + ' (experimental)' : 'Standard'} : {}),
      'Machine graphics': (grayscale ? 'Grayscale' : 'Original designs') + (count ? ' · ' + count + ' customized' : ''),
    });
  }
  function graphicsChanged(machine) {
    updateOverrideCount(); invalidate(); ready();
    if (inspected && (!machine || inspected === machine.id)) inspect(inspected);
  }
  const graphicsDialog = createGraphicsDialog({
    elements:Object.fromEntries(['selected-previews', 'selected-previews-list', 'selected-previews-count',
      'close-selected-previews', 'graphics-all-grayscale', 'graphics-all-original'].map(id => [id, $(id)])),
    configuration:graphicsConfiguration, snapshots:createSnapshotService(), onChange:graphicsChanged, beforeOpen:() => stopAudio(),
  });
  window.addEventListener('pagehide', graphicsDialog.clear);
  $('preview-selected').onclick = () => graphicsDialog.open({machines:selectedMachines(), screenTemplate:kit?.screenTemplate});
  // osApplied counts OS loads that set the memory and selection (layout restores wait for them).
  let ticket = 0, checkingOs = false, osApplied = 0;
  // An OS check is over (or abandoned): the option and layouts can change memory again.
  const endOsCheck = () => { checkingOs = false; memoryInput.disabled = !expandedKit; $('restore').disabled = false; };
  async function acceptFirmware(file, isCurrent = () => true) {
    if (!file || !kit || !isCurrent()) return false;
    wizard.clearSourceBackup();
    const current = ++ticket; invalidate();
    stock = null; ready();
    try {
      // The OS sets the memory choice, so neither it nor a layout can change it while it is checked.
      memoryInput.disabled = $('restore').disabled = true; checkingOs = true;
      wizard.setFirmwareState('checking');
      $('firmware-result').textContent = 'Verifying ' + file.name + '…';
      const recovered = await core.recoverFirmware(kit, await readBounded(file, 16 * 1024 ** 2), numbers);
      if (current !== ticket || !isCurrent()) return false;
      stock = recovered.stock;
      // An open-xy OS whose module base and heap edits match a layout sets the choice
      // to it, even when its machines cannot be rebuilt here. Stock OS, or an OS of
      // no known layout, keeps the current one. A change is said, never silent.
      const memoryChange = recovered.memory ? setExpandedMemory(recovered.memory === 'expanded') : '';
      const memoryNote = memoryChange && ' · ' + memoryChange;
      if (recovered.selected) {
        osApplied++;  // a layout read meanwhile would undo it (a memory change is caught by itself)
        selected = recovered.selected; selectedExtensions = recovered.extensions ?? [];
        renderMachines(); renderExtensions();
        $('firmware-result').textContent = '✓ open-xy OS with ' +
          selectionNames().join(' + ') + memoryNote;
      } else {
        $('firmware-result').textContent = (recovered.earlier
          ? '✓ OP–XY OS 1.2.1 recovered from an earlier open-xy OS · choose your machines and extensions again'
          : '✓ OP–XY OS 1.2.1 verified · ' + file.name) + memoryNote;
      }
      wizard.setFirmwareState('loaded');
      $('files-title').focus();
      // One link for what this OS set, after clearing the status so a selection
      // that cannot be built says why.
      // The check is over: the option can change again, and the link below may suggest it.
      endOsCheck();
      status(); recalculate();
      ready();
      return true;
    } catch (error) {
      if (current !== ticket || !isCurrent()) return false;
      $('firmware-result').textContent = 'OS not loaded';
      fail(error); ready();
      return false;
    } finally {
      if (current === ticket) endOsCheck();
      if (current === ticket && !stock) {
        wizard.setFirmwareState('empty');
        $('firmware-result').textContent = 'OS not loaded';
      }
    }
  }
  function node(tag, text, className) {
    const el = document.createElement(tag); if (text !== undefined) el.textContent = text;
    if (className) el.className = className; return el;
  }
  function facts(target, values) {
    target.replaceChildren(...Object.entries(values).flatMap(([name, value]) => [node('dt', name), node('dd', value)]));
  }
  function sourceLinks(source) {
    const link = (text, url) => {
      const anchor = node('a', text);
      anchor.href = url; anchor.target = '_blank'; anchor.rel = 'noopener noreferrer';
      return anchor;
    };
    return ['Maintained by ', link('@' + source.maintainer, 'https://github.com/' + source.maintainer),
      ' · ', link(core.sourceName(source), source.repository),
      ' · ', link('Source at ' + source.tag, core.sourceUrl(source))];
  }
  function describeMachine(target, m) {
    const field = name => target.querySelector('.' + name);
    field('detail-kind').textContent = {synth: 'Synthesizer', effect: 'Send effect', drum: 'Synthesis drum kit', sample: 'Sample engine', player: 'Note player'}[m.kind];
    field('detail-name').textContent = m.name; field('detail-description').textContent = m.description;
    // Each catalog keyword lists every machine that shares it.
    field('detail-keywords').hidden = !m.keywords?.length;
    field('detail-keywords').replaceChildren(...(m.keywords ?? []).map(keyword => {
      const button = node('button', keyword, 'tag'); button.type = 'button';
      button.setAttribute('aria-label', 'Show all ' + keyword + ' machines');
      button.onclick = () => {
        Object.assign(browsing, {query: '', kind: 'all', keywords: [keyword], favoritesOnly: false});
        syncBrowsing(); renderMachines();
      };
      return button;
    }));
    // Explain the practical voice-count tradeoff immediately on hover or focus.
    const high = core.cpuHigh(m);
    field('detail-cpu-high').hidden = !high;
    cpuBadge(field('detail-cpu-high'), high);
    field('community-notice').hidden = m.origin !== 'catalog';
    // Credit the catalog's maintainer and repository, with the exact release source;
    // Open-XY's own catalog machines (core.ORIGINS) name the project instead.
    if (m.origin === 'catalog') {
      field('detail-source').replaceChildren(...sourceLinks(m.source));
    } else if (m.origin === 'official') {
      field('detail-source').replaceChildren('Open-XY machine', ...sourceLinks(m.source).slice(2));
    } else field('detail-source').textContent = 'Your machine, from this kit';
  }
  function sizeMachineSummary() {
    // Overlapping, inert copies let CSS reserve the tallest summary in this kit,
    // including wrapped attribution and the notice, at any window or font size.
    const summary = $('machine-summary');
    for (const m of kit.machines) {
      const sizing = summary.cloneNode(true);
      sizing.removeAttribute('id');
      sizing.querySelectorAll('[id]').forEach(el => el.removeAttribute('id'));
      sizing.classList.add('summary-sizing');
      sizing.inert = true; sizing.setAttribute('aria-hidden', 'true');
      describeMachine(sizing, m);
      summary.parentElement.append(sizing);
    }
  }
  async function inspect(id) {
    stopPreview();
    const ticket = ++previewRevision;
    inspected = id; const m = kit.machines.find(x => x.id === id);
    describeMachine($('machine-summary'), m);
    $('art-preview').replaceChildren();
    $('audition-controls').replaceChildren();
    stopAudio = () => {};
    if (m.preview) {
      $('art-caption').textContent = 'Audition the sound and screen. The OP–XY’s own envelopes and effects aren’t included.';
    } else {
      $('art-preview').append(node('span', 'No browser preview for this machine yet.', 'art-placeholder'));
      $('art-caption').textContent = '';
    }
    $('controls').replaceChildren(...m.controls.map(c => node('div', c, 'control')));
    const own = kit.objects.filter(o => o.owner === id).flatMap(o => o.sections.filter(s => s.group));
    facts($('machine-facts'), {'Code and data': kib(own.filter(s => s.type !== 8).reduce((n, s) => n + s.size, 0)),
      'Working memory': kib(own.filter(s => s.type === 8).reduce((n, s) => n + s.size, 0)),
      // The numbers this selection gives it, or those it asks for: none, from SDK 1.5, until selected.
      ...(own => ({[m.kind === 'player' ? 'Player type' : m.kind === 'effect' ? 'Effect number' : 'Engine number']: own.player ?? own.effect ?? own.engine ?? 'When selected',
        'Page': own.page ?? 'When selected'}))(linked?.numbers?.[m.id] ?? m.identity),
      ...(m.source ? {'Release': m.source.tag, 'Commit': m.source.commit.slice(0, 12)} : {})});
    document.querySelectorAll('#machines button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.id === id)));
    if (m.preview) {
      try {
        const wasm = core.decode64(m.preview.wasm);
        core.requireThat(await core.sha256(wasm) === m.preview.sha256, 'Preview module hash mismatch');
        const compiled = await WebAssembly.compile(wasm);
        if (ticket !== previewRevision) return;
        const display = new retained.PreviewDisplay(compiled);
        const renderer = new retained.SvgDisplay(core.selectArtwork(m, graphicsConfiguration.modeFor(m)), m.preview.graphics);
        const ns = 'http://www.w3.org/2000/svg', canvas = document.createElementNS(ns, 'svg');
        canvas.setAttribute('viewBox', '0 0 480 220'); canvas.setAttribute('aria-label', m.name + ' display preview');
        // The device page: the stock four-control header under the machine's drawing,
        // which can cover it, as on the device.
        const header = document.createElementNS(ns, 'g');
        canvas.append(header, renderer.root); $('art-preview').replaceChildren(canvas);
        // Eight parameter words; each control position shows the parameter last edited there.
        const parameters = Object.assign(Array(8).fill(0), m.preview.defaults), shown = [0, 1, 2, 3];
        // A synth's Shift row (SDK 1.13): render's words 8..15, played at their defaults.
        const shiftWords = m.preview.shift ?? [];
        const voiceWords = () => [...parameters, ...shiftWords];
        let serial = 0, previous = 0, handle, cancelled = false;
        const noteActivity = new retained.NoteActivity();
        // A small keyboard from middle C: a key plays its note while held. The preview plays
        // one note at a time, the newest held key; letting it go returns to the one before.
        const names = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'], held = [];
        const keyboard = node('div', undefined, 'preview-keys');
        keyboard.id = 'preview-keys'; keyboard.setAttribute('role', 'group'); keyboard.setAttribute('aria-label', 'Preview keyboard');
        for (let note = 60; note <= 77; note++) {
          const black = [1, 3, 6, 8, 10].includes(note % 12), name = names[note % 12] + (Math.floor(note / 12) - 1);
          const key = node('button', note % 12 === 0 ? name : undefined, 'preview-key' + (black ? ' black' : ''));
          key.type = 'button'; key.setAttribute('aria-label', name);
          const press = () => {
            if (key.dataset.down === 'true') return;
            key.dataset.down = 'true'; held.push(note); noteActivity.noteOn(note); trigger(); audio.play(note);
          };
          const lift = () => {
            if (key.dataset.down !== 'true') return;
            key.dataset.down = 'false';
            const sounding = held.at(-1) === note;
            held.splice(held.indexOf(note), 1);
            noteActivity.noteOff(note); frame(0);
            if (sounding && held.length) audio.play(held.at(-1)); else audio.release(note);
          };
          key.onpointerdown = e => { e.preventDefault(); key.setPointerCapture(e.pointerId); key.focus(); press(); };
          key.onpointerup = key.onpointercancel = key.onlostpointercapture = lift;
          key.onkeydown = e => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); if (!e.repeat) press(); } };
          key.onkeyup = e => { if (e.key === ' ' || e.key === 'Enter') lift(); };
          key.onblur = lift;
          keyboard.append(key);
        }
        const volume = node('label', 'Volume', 'audition-volume'), level = node('input');
        level.type = 'range'; level.min = '0'; level.max = '100'; level.value = '50'; level.setAttribute('aria-label', 'Preview volume');
        volume.append(level);
        const audio = new audition(compiled, voiceWords(), (playing, error) => {
          if (!cancelled) frame(0);
          if (error && ticket === previewRevision) $('art-caption').textContent = 'Audio unavailable: ' + error;
        });
        stopAudio = () => {
          held.length = 0;
          for (const key of keyboard.children) key.dataset.down = 'false';
          noteActivity.panic(); frame(0); audio.stop();
        };
        level.oninput = () => audio.setVolume(Number(level.value) / 100);
        $('audition-controls').append(keyboard, volume);
        const reduced = matchMedia('(prefers-reduced-motion: reduce)');
        function frame(dt = 34) {
          const input = shownControls(parameters, shown);
          // The preview is one track: a changing track would tell the machine to start over.
          const row = shiftWords.length ? [0, 0, [0, 0, 0, 0], null, {pan:0, level:0}, 0,
            {held:0, line:shown.some(slot => slot >= 4) ? 1 : 0, values:shiftWords}] : [];
          renderer.apply(display.tick(input, dt, serial, 0, reduced.matches, noteActivity, ...row));
          // The machine's header text, or the stock percentage of the shown parameter.
          header.innerHTML = retained.headerSvg(shown.map(slot => m.controls[slot].toLowerCase()),
            shown.map((slot, i) => renderer.header[i] || String(Math.round(parameters[slot] / 0x7fffffff * 100))));
        }
        const controls = m.controls.map((name, i) => {
          const label = node('label', undefined, 'preview-control'), input = node('input');
          label.title = name;
          label.append(node('span', name));
          input.type = 'range'; input.min = '0'; input.max = '1000'; input.step = '1';
          input.value = String(parameters[i] / 0x7fffffff * 1000); input.setAttribute('aria-label', name + ' preview');
          input.oninput = () => {
            parameters[i] = Math.round(Number(input.value) / 1000 * 0x7fffffff);
            shown.splice(0, 4, ...editedShown(shown, m.positions, i, m.pages));
            frame(0); audio.setParameters(voiceWords());
          };
          label.append(input); return label;
        });
        $('controls').replaceChildren(...controls);
        const trigger = () => { serial = serial + 1 >>> 0; frame(0); };
        canvas.setAttribute('role', 'button'); canvas.setAttribute('tabindex', '0'); canvas.setAttribute('aria-label', 'Trigger ' + m.name + ' display preview');
        canvas.onclick = trigger; canvas.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); trigger(); } };
        frame(0); trigger();
        function animate(time) {
          if (cancelled) return;
          if (time - previous >= 32 && !document.hidden && !document.querySelector('[data-page="2"]').hidden) {
            frame(Math.min(100, Math.round((time - previous) * 1024 / 1000))); previous = time;
          }
          handle = requestAnimationFrame(animate);
        }
        if (!reduced.matches) handle = requestAnimationFrame(animate);
        stopPreview = () => { cancelled = true; cancelAnimationFrame(handle); audio.destroy(); };
      } catch (error) { if (ticket === previewRevision) $('art-caption').textContent = 'Display preview unavailable: ' + error.message; }
    }
  }
  // The browsing controls, matched to `browsing`.
  function syncBrowsing() {
    $('machine-search').value = browsing.query;
    $('machine-sort').value = browsing.sort;
    document.querySelectorAll('[data-filter]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.filter === browsing.kind)));
    $('favorites-filter').setAttribute('aria-pressed', String(browsing.favoritesOnly));
    for (const chip of $('keyword-filters').querySelectorAll('.keyword-filter')) chip.setAttribute('aria-pressed', String(browsing.keywords.includes(chip.dataset.keyword)));
    fitKeywords();
  }
  // Keyword filters take at most three lines, still in alphabetical order: the least used
  // (core.keywordsByUse) give way first, a keyword in use as a filter always shows, and
  // "+N more" shows the rest. Fitted again when the row's width changes, including when the
  // Machines step first appears (a hidden row has no width to measure).
  const KEYWORD_LINES = 3;
  let keywordsExpanded = false, keywordsWidth = 0;
  function fitKeywords() {
    const row = $('keyword-filters'), more = row.querySelector?.('.keyword-more');
    if (!more) return;
    const chips = [...row.querySelectorAll('.keyword-filter')];
    chips.forEach(chip => chip.hidden = false);
    more.hidden = true;
    keywordsWidth = row.clientWidth;
    if (!keywordsWidth || !chips.length) return;
    const gap = parseFloat(getComputedStyle(row).rowGap) || 0;
    const limit = KEYWORD_LINES * chips[0].offsetHeight + (KEYWORD_LINES - 1) * gap + 1;
    if (row.offsetHeight <= limit) return;
    // Least used first, keeping the ones filtering the list.
    const giving = core.keywordsByUse(kit.machines).map(k => k.keyword).reverse()
      .filter(keyword => !browsing.keywords.includes(keyword));
    const hidden = [];
    more.hidden = false;
    const label = () => {
      more.textContent = keywordsExpanded ? 'Fewer' : '+' + hidden.length + ' more';
      more.setAttribute('aria-expanded', String(keywordsExpanded));
      more.setAttribute('aria-label', keywordsExpanded ? 'Show fewer keywords' : 'Show ' + hidden.length + ' more keywords');
    };
    label();
    for (const keyword of giving) {
      if (row.offsetHeight <= limit && hidden.length) break;
      chips.find(chip => chip.dataset.keyword === keyword).hidden = true;
      hidden.push(keyword);
      label();
    }
    if (keywordsExpanded) { chips.forEach(chip => chip.hidden = false); label(); }
  }
  if (typeof ResizeObserver === 'function') new ResizeObserver(() => {
    if ($('keyword-filters').clientWidth !== keywordsWidth) fitKeywords();
  }).observe($('keyword-filters'));
  function clearBrowsing() {
    Object.assign(browsing, {query: '', kind: 'all', keywords: [], favoritesOnly: false});
    syncBrowsing(); renderMachines();
  }
  // Kinds and keywords this kit has: the others are not offered as filters.
  function renderBrowsing() {
    document.querySelectorAll('[data-filter]').forEach(b =>
      b.hidden = b.dataset.filter !== 'all' && !kit.machines.some(m => m.kind === b.dataset.filter));
    if (!kit.machines.some(m => m.kind === browsing.kind)) browsing.kind = 'all';
    const counts = new Map(core.MACHINE_KEYWORDS.map(keyword => [keyword, kit.machines.filter(m => m.keywords?.includes(keyword)).length]));
    const offered = core.MACHINE_KEYWORDS.filter(keyword => counts.get(keyword));
    browsing.keywords = browsing.keywords.filter(keyword => offered.includes(keyword));
    $('keyword-filters').hidden = !offered.length;
    $('keyword-filters').replaceChildren(...offered.map(keyword => {
      const chip = node('button', keyword, 'keyword-filter'); chip.type = 'button'; chip.dataset.keyword = keyword;
      const count = node('span', String(counts.get(keyword)), 'keyword-count'); count.setAttribute('aria-hidden', 'true');
      chip.append(count); chip.setAttribute('aria-label', keyword + ', ' + counts.get(keyword) + (counts.get(keyword) === 1 ? ' machine' : ' machines'));
      chip.onclick = () => {
        browsing.keywords = browsing.keywords.includes(keyword) ? browsing.keywords.filter(t => t !== keyword) : [...browsing.keywords, keyword];
        syncBrowsing(); renderMachines();
      };
      return chip;
    }));
    const more = node('button', undefined, 'keyword-more'); more.type = 'button'; more.hidden = true;
    more.onclick = () => { keywordsExpanded = !keywordsExpanded; fitKeywords(); more.focus(); };
    $('keyword-filters').append(more);
    syncBrowsing();
  }
  function renderMachines() {
    $('machines').style.setProperty('--machine-count', kit.machines.length);
    $('machines').replaceChildren();
    // Each row keeps its collection index, and so its memory-map colour, in any order.
    const rows = core.browseMachines(kit.machines, {...browsing, favorites: browsing.favoritesOnly ? favorites : null});
    const filtered = Boolean(browsing.query.trim() || browsing.kind !== 'all' || browsing.keywords.length || browsing.favoritesOnly);
    const hiddenSelected = selected.filter(id => !rows.some(({m}) => m.id === id)).length;
    $('browse-result').textContent = !filtered ? '' : rows.length + ' of ' + kit.machines.length + ' machines' +
      (hiddenSelected ? ' · ' + hiddenSelected + ' selected not shown' : '');
    if (!rows.length) {
      const empty = node('p', browsing.favoritesOnly && !favorites.size ? 'No favorites yet. Star a machine to keep it here.'
        : 'No machines match.', 'machine-empty');
      const clear = node('button', 'Clear filters', 'text-button'); clear.type = 'button';
      clear.onclick = () => { clearBrowsing(); $('machine-search').focus(); };
      empty.append(' ', clear); $('machines').append(empty);
    }
    rows.forEach(({m, index}) => {
      const row = node('article', undefined, 'machine' + (selected.includes(m.id) ? ' selected' : ''));
      row.dataset.origin = m.origin;
      row.style.setProperty('--machine', colors[index % colors.length]);
      const label = node('label'), input = node('input'); input.type = 'checkbox'; input.checked = selected.includes(m.id);
      input.setAttribute('aria-label', 'Include ' + m.name); label.append(input);
      const button = node('button'); button.dataset.id = m.id; button.setAttribute('aria-pressed', String(inspected === m.id));
      const name = node('span', m.name, 'machine-name');
      // Its catalog maintainer, beside the name; a personal kit's own machines have none.
      if (core.machineAuthor(m)) name.append(' ', node('span', '(' + core.machineAuthor(m) + ')', 'machine-author'));
      button.append(name, node('span', m.description, 'machine-desc'));
      const meta = node('span', undefined, 'machine-meta');
      meta.append(node('span', {synth: 'Synth', effect: 'Effect', drum: 'Drum kit', sample: 'Sample', player: 'Player'}[m.kind]), node('span', m.controls.length + (m.controls.length === 1 ? ' control' : ' controls')));
      const ownBytes = kit.objects.filter(o => o.owner === m.id)
        .flatMap(o => o.sections.filter(s => s.group)).reduce((total, s) => total + s.size, 0);
      const memory = node('span', kib(ownBytes), 'machine-memory');
      memory.title = 'Code, data and working memory. Shared code is counted separately below.';
      meta.append(memory);
      const originLabel = {bundled: 'Core', official: 'Core', catalog: 'Community', personal: 'Yours'}[m.origin];
      meta.append(node('span', originLabel, 'machine-origin'));
      const high = core.cpuHigh(m);
      if (high) {
        const badge = node('span', 'CPU-high', 'machine-cpu-high');
        meta.append(badge);
      }
      button.append(meta);
      const cpuMark = meta.querySelector('.machine-cpu-high');
      if (cpuMark) cpuBadge(cpuMark, high);
      button.onclick = () => inspect(m.id);
      input.onchange = () => {
        selected = input.checked ? [...selected, m.id] : selected.filter(id => id !== m.id);
        row.classList.toggle('selected', input.checked); invalidate(); recalculate();
      };
      const glyph = node('span', undefined, 'machine-icon');
      glyph.append(visuals.icon(m.kind, m.icon));
      // A row stays listed when unstarred under Favorites, until the list changes, so a
      // mistaken click can be undone in place.
      // Outlined or filled, so its state does not rest on colour alone.
      const star = node('button', undefined, 'machine-favorite'); star.type = 'button';
      star.setAttribute('aria-label', 'Favorite ' + m.name);
      const showFavorite = () => {
        star.textContent = favorites.has(m.id) ? '★' : '☆';
        star.setAttribute('aria-pressed', String(favorites.has(m.id)));
      };
      showFavorite();
      star.onclick = () => { toggleFavorite(m.id); showFavorite(); };
      row.append(label, glyph, button, star); $('machines').append(row);
    });
  }
  function selectionNames() {
    return [...kit.machines.filter(m => selected.includes(m.id)),
      ...(kit.extensions ?? []).filter(e => selectedExtensions.includes(e.id))].map(item => item.name);
  }
  function renderExtensions() {
    const entries = kit.extensions ?? [];
    $('extensions').replaceChildren();
    $('extensions-empty').hidden = entries.length !== 0;
    for (const extension of entries) {
      const row = node('article', undefined, 'machine' + (selectedExtensions.includes(extension.id) ? ' selected' : ''));
      row.style.setProperty('--machine', 'var(--machine-1)');
      const label = node('label'), input = node('input');
      input.type = 'checkbox'; input.checked = selectedExtensions.includes(extension.id);
      input.setAttribute('aria-label', 'Include ' + extension.name); label.append(input);
      const button = node('button'); button.dataset.id = extension.id;
      button.setAttribute('aria-pressed', String(inspectedExtension === extension.id));
      // The first selected extension also links the shared extension runtime.
      const others = selectedExtensions.some(id => id !== extension.id);
      const ownBytes = kit.objects.filter(o => o.extension === extension.id || (!others && o.service === 'extensions'))
        .flatMap(o => o.sections.filter(s => s.group)).reduce((total, s) => total + s.size, 0);
      const meta = node('span', undefined, 'machine-meta');
      meta.append(node('span', 'Extension'), node('span', kib(ownBytes), 'machine-memory'),
        node('span', {official: 'Core', catalog: 'Community', personal: 'Yours'}[extension.origin], 'machine-origin'));
      button.append(node('span', extension.name, 'machine-name'), meta);
      button.onclick = () => inspectExtension(extension.id);
      const conflict = entries.find(e => e.id !== extension.id && selectedExtensions.includes(e.id) &&
        core.sharedClaim([e, extension]));
      if (conflict && !input.checked) {
        input.disabled = true;
        const claim = core.sharedClaim([conflict, extension]);
        const message = node('p', 'Uses ' + core.claimLabel([conflict, extension], claim) + ', like ' + conflict.name +
          '. Deselect it first.', 'extension-conflict');
        message.id = 'conflict-' + extension.id; input.setAttribute('aria-describedby', message.id); button.append(message);
      }
      input.onchange = () => {
        selectedExtensions = core.extensionSelection(kit, input.checked ? [...selectedExtensions, extension.id]
          : selectedExtensions.filter(id => id !== extension.id));
        invalidate(); renderExtensions(); recalculate();
        document.querySelector('#extensions input[aria-label="Include ' + CSS.escape(extension.name) + '"]')?.focus();
      };
      const glyph = node('span', undefined, 'machine-icon'); glyph.append(visuals.icon('controls'));
      row.append(label, glyph, button); $('extensions').append(row);
    }
    inspectExtension(entries.find(e => e.id === inspectedExtension)?.id ?? entries[0]?.id);
  }
  function inspectExtension(id) {
    const extension = kit.extensions?.find(e => e.id === id);
    inspectedExtension = extension?.id;
    $('extension-details').hidden = !extension;
    if (!extension) return;
    $('extension-name').textContent = extension.name;
    $('extension-description').textContent = extension.description;
    $('extension-note').textContent = extension.note ?? '';
    $('extension-note').hidden = !extension.note;
    document.querySelectorAll('#extensions button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.id === id)));
  }
  function recalculate() {
    updateOverrideCount();
    linked = null; $('selection-count').textContent = selected.length + ' selected';
    $('extension-memory').textContent = '';
    $('memory-map').replaceChildren(); $('memory-legend').replaceChildren();
    try {
      if (!selected.length) { $('room-value').textContent = 'Choose at least one machine'; $('capacity-details').replaceChildren(); ready(); return; }
      const memory = chosenKit();
      linked = core.linkKit(memory, selected, numbers, selectedExtensions);
      const allocations = kit.machines.filter(m => selected.includes(m.id)).map(m => ({name: m.name,
        size: linked.segments.filter(s => s.owner === m.id).reduce((n, s) => n + s.size, 0),
        color: colors[kit.machines.indexOf(m) % colors.length]}));
      allocations.push({name: 'Shared', size: linked.sizes.allocated - allocations.reduce((n, a) => n + a.size, 0), color: 'var(--shared)'},
        {name: 'Available', size: linked.sizes.available, color: 'var(--paper)'}, {name: 'System', size: memory.profile.table_bytes, color: 'var(--tables)'});
      for (const a of allocations) {
        const segment = node('div', undefined, 'memory-segment'); segment.style.width = a.size / memory.profile.reserved_bytes * 100 + '%';
        segment.style.background = a.color; segment.title = a.name + ': ' + kib(a.size); $('memory-map').append(segment);
        const legend = node('span', a.name + ' ' + kib(a.size)); legend.style.setProperty('--swatch', a.color); $('memory-legend').append(legend);
      }
      $('room-value').textContent = kib(linked.sizes.allocated) + ' of ' + kib(core.moduleBytes(memory.profile)) + ' used';
      $('extension-memory').textContent = $('room-value').textContent;
      $('memory-map').setAttribute('aria-label', allocations.map(a => a.name + ': ' + kib(a.size)).join(', '));
      facts($('capacity-details'), {'Code and data': kib(linked.sizes.initialized),
        'Working memory': kib(linked.sizes.bss),
        'Available': kib(linked.sizes.available)});
      status();
    } catch (error) {
      // A selection that does not fit standard memory points to the larger option this site offers.
      if (error.capacity === true && !expandedMemory())
        error.message = roomHint(error.message, 'to make room', selected, numbers, selectedExtensions);
      $('room-value').textContent = 'Selection cannot be built'; $('extension-memory').textContent = error.message; fail(error);
    }
    ready();
  }
  async function build() {
    if (builderPreview || !acknowledged) return;
    const ticket = revision; $('build').disabled = true; status('Linking and checking your build…');
    try {
      const selection = [...selected], settings = graphicsConfiguration.settings(selectedMachines()), extensions = core.extensionSelection(kit, selectedExtensions);
      const expanded = expandedMemory(), memory = kitFor(expanded), result = core.linkKit(memory, selection, numbers, extensions);
      const files = await core.buildAssets(memory, selection, catalog, result.numbers, settings, result.extensions);
      files['os.ldr'] = await core.patchFirmware(memory, result, stock, {profiling: false});
      const hashes = {};
      for (const [name, bytes] of Object.entries(files)) hashes[name] = await core.sha256(bytes);
      // Every number this firmware has given out, for the next build (core.readLayout).
      const nextLayout = {format: 'opxy-layout/1', profile: kit.profile.id, selected: selection, extensions, machines: result.numbers,
        ...(expanded ? {memory: 'expanded', reserved_bytes: memory.profile.reserved_bytes} : {})};
      const encode = value => new TextEncoder().encode(JSON.stringify(value, null, 2) + '\n');
      files['layout.json'] = encode(nextLayout);
      files['build.json'] = encode({format: 'opxy-kit-build/1', version: kit.version, source_sha256: kit.profile.stock_sha256,
        settings, sizes: result.sizes, ...(expanded ? {reserved_bytes: memory.profile.reserved_bytes} : {}), files: hashes, hardware_qualified: false, cpu_measured: false});
      const zip = core.zipFiles(files);
      if (ticket !== revision) return;
      layout = nextLayout; built = files; rememberNumbers(result.numbers); downloadUrl = URL.createObjectURL(new Blob([zip], {type: 'application/zip'}));
      $('download').href = $('flow-download').href = downloadUrl;
      const modes = new Set(kit.machines.filter(machine => selection.includes(machine.id) && machine.artwork).map(machine => core.graphicsForMachine(machine, settings)));
      $('download-summary').textContent = selectionNames().concat(modes.has('grayscale') ? ['Grayscale machine graphics'] : [],
        expanded ? [expandedSize() + ' experimental memory (may be unstable)'] : []).join(' + ');
      $('download-graphics').textContent = modes.size > 1 ? 'Machine graphics: per-machine settings' : modes.has('grayscale')
        ? 'Machine graphics: grayscale' : modes.size ? 'Machine graphics: original designs' : 'Machine graphics: stock screens unchanged';
      $('bundle-art').replaceChildren(visuals.bundle(Object.keys(files)));
      $('output-hash').textContent = 'OS (os.ldr) SHA-256: ' + hashes['os.ldr'];
      document.querySelector('.steps [data-step="4"]').disabled = false;
      status(); navigate(4);
    } catch (error) { if (ticket === revision) fail(error); } finally { ready(); }
  }
  document.querySelectorAll('[data-step]').forEach(b => b.onclick = () => navigate(Number(b.dataset.step)));
  $('choose').onclick = () => { navigate(2); recalculate(); };
  const graphicsInput = $('grayscale-graphics');
  graphicsInput.checked = false;
  graphicsInput.onchange = () => {
    if (graphicsConfiguration.setAll(selectedMachines(), graphicsInput.checked ? 'grayscale' : 'colorful')) graphicsChanged();
    else updateOverrideCount();
  };
  $('choose-extensions').onclick = () => { navigate(3); recalculate(); };
  $('build').onclick = build;
  // Sets the memory choice (only offered memory can be chosen) and describes a
  // change for the caller's message, or returns '' when it stays the same.
  function setExpandedMemory(value) {
    const before = expandedMemory();
    memoryInput.checked = value && expandedKit !== null;
    $('capacity-foot').textContent = kib(core.moduleBytes(chosenKit().profile)) + ' for machines' +
      (memoryInput.checked ? ' (experimental)' : '') + '; CPU load is not measured';
    // A collection without the option clears it with nothing to say.
    return memoryInput.checked === before || !expandedKit ? '' : memoryInput.checked
      ? 'it uses ' + expandedSize() + ' experimental memory, now turned on'
      : 'it uses standard memory, so ' + expandedSize() + ' is turned off';
  }
  memoryInput.onchange = () => {
    setExpandedMemory(memoryInput.checked);
    invalidate(); recalculate();
  };
  $('firmware').onchange = e => acceptFirmware(e.target.files[0]);
  const drop = $('firmware-drop');
  for (const type of ['dragover', 'dragleave', 'drop']) drop.addEventListener(type, e => {
    e.preventDefault(); drop.dataset.drag = String(type === 'dragover');
    if (type === 'drop') acceptFirmware(e.dataTransfer.files[0]);
  });
  document.querySelectorAll('[data-filter]').forEach(b => b.onclick = () => {
    browsing.kind = b.dataset.filter; syncBrowsing(); renderMachines();
  });
  $('favorites-filter').onclick = () => { browsing.favoritesOnly = !browsing.favoritesOnly; syncBrowsing(); renderMachines(); };
  $('machine-search').oninput = e => { browsing.query = e.target.value; renderMachines(); };
  $('machine-sort').onchange = e => { browsing.sort = e.target.value; renderMachines(); };
  // Each saved preset: choose it, update it to the current selection, or delete it. A machine
  // this collection lacks is counted apart, since choosing the preset cannot select it here.
  // Rendered again whenever the menu opens, so Update reflects the selection made since.
  function renderPresets() {
    $('presets-empty').hidden = presets.length > 0;
    $('presets-list').hidden = !presets.length;
    $('presets-list').replaceChildren(...presets.map(preset => {
      const {selected: offered, missing} = kit ? core.applyPreset(kit, preset) : {selected: preset.machines, missing: []};
      const item = node('li', undefined, 'preset');
      const choose = node('button', undefined, 'preset-apply'); choose.type = 'button';
      choose.append(node('span', preset.name, 'preset-name'),
        node('span', offered.length + (offered.length === 1 ? ' machine' : ' machines') +
          (missing.length ? ' · ' + missing.length + ' not here' : ''), 'preset-count'));
      choose.title = [...offered.map(id => kit?.machines.find(m => m.id === id)?.name ?? id), ...missing].join(', ');
      choose.onclick = () => choosePreset(preset);
      // Saves the current selection into this preset; nothing to do when it already matches.
      const update = node('button', 'Update', 'preset-update'); update.type = 'button';
      update.setAttribute('aria-label', 'Update preset ' + preset.name + ' to the selected machines');
      update.disabled = !selected.length || core.presetMatches(preset, selected);
      update.title = !selected.length ? 'Select machines to update this preset'
        : update.disabled ? 'Already the selected machines'
        : selected.length === 1 ? 'Replace with the selected machine' : 'Replace with the ' + selected.length + ' selected machines';
      update.onclick = () => {
        storePresets(core.savePreset(presets, preset.name, selected));
        $('preset-message').textContent = 'Updated “' + preset.name + '”: ' + selected.length +
          (selected.length === 1 ? ' machine.' : ' machines.');
        // Its row is drawn again; keep focus on it.
        [...$('presets-list').querySelectorAll('.preset-apply')].find(b => b.querySelector('.preset-name').textContent === preset.name)?.focus();
      };
      const remove = node('button', '✕', 'preset-delete'); remove.type = 'button';
      remove.setAttribute('aria-label', 'Delete preset ' + preset.name);
      remove.onclick = () => {
        storePresets(core.deletePreset(presets, preset.name));
        $('preset-message').textContent = 'Deleted “' + preset.name + '”.';
        $('preset-name').focus();
      };
      item.append(choose, update, remove);
      return item;
    }));
  }
  $('machine-presets').addEventListener('toggle', () => {
    if ($('machine-presets').open) { $('preset-message').textContent = ''; renderPresets(); }
  });
  // Choosing a preset replaces the selection, as ticking its machines would.
  function choosePreset(preset) {
    const {selected: chosen, missing} = core.applyPreset(kit, preset);
    selected = chosen;
    invalidate(); renderMachines(); recalculate();
    $('machine-presets').open = false;
    status('Preset “' + preset.name + '”: ' + chosen.length + (chosen.length === 1 ? ' machine' : ' machines') + ' selected.' +
      (missing.length ? ' Not in this collection: ' + missing.join(', ') + '.' : ''));
  }
  // The menu closes as menus do: Escape returns to its button, and a click elsewhere leaves it.
  document.addEventListener('keydown', event => {
    if (event.key !== 'Escape' || !$('machine-presets').open) return;
    $('machine-presets').open = false; $('machine-presets').querySelector('summary').focus();
  });
  document.addEventListener('pointerdown', event => {
    if ($('machine-presets').open && !$('machine-presets').contains(event.target)) $('machine-presets').open = false;
  });
  $('preset-form').onsubmit = event => {
    event.preventDefault();
    try {
      const name = $('preset-name').value;
      const replaced = presets.some(p => p.name.localeCompare(name.trim(), undefined, {sensitivity: 'base'}) === 0);
      storePresets(core.savePreset(presets, name, selected));
      $('preset-name').value = '';
      $('preset-message').textContent = (replaced ? 'Updated “' : 'Saved “') + name.trim().replace(/\s+/g, ' ') + '”: ' +
        selected.length + (selected.length === 1 ? ' machine.' : ' machines.');
    } catch (error) { $('preset-message').textContent = error.message; }
  };
  $('restore').onchange = async e => {
    try {
      // The input is disabled while an OS is checked. An OS check that started, or even
      // finished, during the read set the memory and selection, so this layout waits.
      const applied = osApplied, collection = kit, memoryBefore = expandedMemory();
      const saved = JSON.parse(new TextDecoder().decode(await readBounded(e.target.files[0], 65536)));
      core.requireThat(!checkingOs && osApplied === applied && kit === collection && expandedMemory() === memoryBefore,
        'Your OS file, the machines or the memory choice changed meanwhile. Load the layout again if you still want it.');
      const loaded = core.readLayout(kit, saved);
      // A layout keeps its machine numbers even when an extension it names is no
      // longer offered; that extension is left out and named.
      const requested = Array.isArray(saved.extensions) ? saved.extensions : [];
      const offered = new Set((kit.extensions ?? []).map(e => e.id));
      const extensions = core.extensionSelection(kit, requested.filter(id => offered.has(id)));
      const missing = requested.filter(id => !offered.has(id));
      // Nothing is applied unless the selection links. A numbers-only layout leaves the
      // current memory choice alone. A layout with a selection restores with the memory
      // it records, or, for an experimental size this site lacks, the current choice. A
      // selection that needs more uses this site's larger option when it is on, and
      // otherwise the error says to turn it on.
      const context = loaded.memoryUnavailable
        ? `This layout used ${mib(loaded.memoryUnavailable)} experimental memory, which this site doesn’t offer. ` : '';
      let wanted = loaded.selected && !loaded.memoryUnavailable ? loaded.expanded === true : expandedMemory(), grown = false;
      if (loaded.selected) {
        const link = memory => core.linkKit(kitFor(memory), loaded.selected, loaded.machines, extensions);
        try { link(wanted); }
        catch (error) {
          if (error.capacity !== true) throw error;
          if (wanted || !expandedKit || !expandedMemory()) {
            error.message = context + (wanted ? error.message
              : roomHint(error.message, 'and load the layout again', loaded.selected, loaded.machines, extensions));
            throw error;
          }
          try { link(true); } catch (larger) { if (larger.capacity === true) larger.message = context + larger.message; throw larger; }
          wanted = grown = true;
        }
      }
      const memoryChange = setExpandedMemory(wanted);
      selectedExtensions = extensions;
      rememberNumbers(loaded.machines);
      if (loaded.selected) selected = [...loaded.selected];
      invalidate(); renderMachines(); renderExtensions(); recalculate();
      // A selection that cannot be built (a numbers-only layout is not linked first) says
      // why after the layout's own message.
      const unbuildable = !linked && selected.length ? ' ' + $('extension-memory').textContent : '';
      // A layout from an experimental size this site lacks gets one message, not two.
      const unavailable = loaded.memoryUnavailable && loaded.selected;
      status('Layout loaded: your machines keep their numbers.' +
        (unavailable ? ` It used ${mib(loaded.memoryUnavailable)} experimental memory, which this site doesn’t offer; it ${
            wanted ? `uses this site’s experimental ${expandedSize()}` : 'builds with standard memory'}.`
          : grown ? ` Its machines need more than the standard memory it used, so they use the experimental ${expandedSize()} you turned on.`
          : memoryChange && ' ' + memoryChange[0].toUpperCase() + memoryChange.slice(1) + '.') +
        // A numbers-only layout keeps the current memory choice; its own is named, not applied.
        (!loaded.selected && (loaded.expanded || loaded.memoryUnavailable)
          ? ' It lists no machines, so its experimental memory choice is not applied; the current one stays.' : '') +
        (missing.length ? ' Extensions this site no longer offers: ' + missing.join(', ') + '.' : '') + unbuildable,
        Boolean(unbuildable));
    } catch (error) { fail(error); } finally { e.target.value = ''; }  // the same file can be chosen again
  };
  $('save-layout').onclick = () => {
    if (!layout) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(layout, null, 2)], {type: 'application/json'}));
    const a = node('a'); a.href = url; a.download = 'layout.json'; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  async function loadCollectionData(data) {
    const next = await core.openKit(data);
    const hadOs = stock != null || checkingOs;
    invalidate(); stock = null; kit = next;
    // A loaded or still-checking OS was for the previous collection: it is loaded again.
    if (hadOs) {
      ticket++; endOsCheck();
      wizard.setFirmwareState('empty'); $('firmware-result').textContent = 'OS not loaded';
      $('firmware').value = '';  // the same os.ldr can be chosen again
    }
    // A tap opens the memory breakdown on screens without hover.
    $('builder-version').textContent = 'v' + kit.version;
    expandedKit = core.offersExpandedMemory(kit.profile) ? core.memoryKit(kit, true) : null;
    memoryInput.disabled = !expandedKit;
    $('expanded-memory-option').hidden = !expandedKit;
    if (expandedKit) $('expanded-memory-size').textContent = expandedSize();
    setExpandedMemory(false);
    selected = kit.machines.filter(core.chosenByDefault).map(machine => machine.id);
    try {
      const remembered = JSON.parse(localStorage.getItem(NUMBERS_KEY) ?? 'null');
      if (remembered) numbers = core.readLayout(kit, {format: 'opxy-layout/1', ...remembered}).machines;
    } catch { numbers = null; }
    catalog = core.decode64(kit.stockCatalog);
    $('notices').textContent = kit.notices;
    $('notices').closest('details').hidden = false;
    sizeMachineSummary(); renderBrowsing(); renderMachines(); renderPresets(); renderExtensions(); inspect(kit.machines[0].id); recalculate();
  }
  document.querySelector('.memory-bar').onclick = e => e.currentTarget.classList.toggle('open');
  try {
    const data = $('kit-data').textContent.trim();
    core.requireThat(data, 'The machines couldn’t load. Reload this page.');
    await loadCollectionData(JSON.parse(data));
  } catch (error) { fail(error); }
  openPreviewLink();
  return {loadCollection:loadCollectionData};
}
