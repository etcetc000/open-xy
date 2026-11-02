// The connector reuses sdk.py's checked builder and device installer. The web
// page never writes firmware, activation records or device files itself.
const CONNECTOR = 'http://127.0.0.1:8920';
const SKIP = new Set(['.git', 'node_modules', '.venv', '__pycache__', '.DS_Store']);
export const MAX_SOURCE_BYTES = 64 * 1024 * 1024;

export function sourceFiles(files) {
  const kept = [...files].filter(file => !(file.webkitRelativePath || file.name).split('/').some(p => SKIP.has(p)));
  if (!kept.length || kept.length > 4096 || kept.reduce((sum, f) => sum + f.size, 0) > MAX_SOURCE_BYTES)
    throw new Error('Choose a machine source folder with at most 4096 files and 64 MiB.');
  if (!kept.some(file => (file.webkitRelativePath || file.name).split('/').at(-1) === 'machine.json'))
    throw new Error('Choose a source folder containing machine.json and definition.json.');
  return kept;
}

export async function encodeFolder(files) {
  const result = [];
  for (const file of sourceFiles(files)) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    let binary = '';
    for (let at = 0; at < bytes.length; at += 16384) binary += String.fromCharCode(...bytes.subarray(at, at + 16384));
    result.push({path:file.webkitRelativePath || file.name, data:btoa(binary)});
  }
  return result;
}

export function isXYIdentity(data) {
  const prefix = [0xf0, 0x7e, 0x21, 0x06, 0x02, 0x00, 0x20, 0x76, 0x21, 0, 1, 0];
  return data?.length === 17 && prefix.every((n, i) => data[i] === n) && data[16] === 0xf7 &&
    [...data.slice(1, 16)].every(n => n < 128);
}

export async function enterRegularMTP(requestAccess = options => navigator.requestMIDIAccess(options)) {
  let permissionTimer;
  const access = await Promise.race([requestAccess({sysex:true}), new Promise((_, reject) => {
    permissionTimer = setTimeout(() => reject(new Error('MIDI permission did not finish. Open COM → M4 MTP on your OP-XY, then connect again.')), 8000);
  })]).finally(() => clearTimeout(permissionTimer));
  const ports = direction => [...access[direction].values()].filter(p => p.state === 'connected' && p.name === 'OP-XY');
  const inputs = ports('inputs'), outputs = ports('outputs');
  // In MTP there are no MIDI ports. Let the checked connector open it as-is.
  if (!inputs.length && !outputs.length) return false;
  if (inputs.length !== 1 || outputs.length !== 1) throw new Error('Connect exactly one OP-XY; no MTP command was sent.');
  const input = inputs[0], output = outputs[0];
  const request = (predicate, bytes) => new Promise((resolve, reject) => {
    const finish = (error, data) => {
      clearTimeout(timer); input.onmidimessage = null;
      error ? reject(error) : resolve(data);
    };
    const timer = setTimeout(() => finish(new Error('OP-XY did not confirm file transfer. Open COM → M4 MTP, then try again.')), 3000);
    input.onmidimessage = event => { if (predicate(event.data)) finish(null, event.data); };
    try { output.send(bytes); } catch (error) { finish(error); }
  });
  try {
    await input.open(); await output.open();
    await request(isXYIdentity, [0xf0, 0x7e, 0x7f, 6, 1, 0xf7]);
    const ack = await request(data => data.length === 11 &&
      [0xf0, 0, 0x20, 0x76, 0x21, 0x40, 0x20, 2, 4].every((n, i) => data[i] === n) && data[10] === 0xf7,
      [0xf0, 0, 0x20, 0x76, 0x21, 0x40, 0x60, 2, 4, 0, 1, 2, 0xf7]);
    if (ack[9] !== 0) throw new Error('OP-XY refused file transfer; no retry was sent.');
    await new Promise(resolve => setTimeout(resolve, 1500));
    return true;
  } finally {
    input.onmidimessage = null;
    await Promise.allSettled([input.close(), output.close()]);
  }
}

export class Client {
  constructor(token, fetcher = (...args) => fetch(...args)) { this.token = token; this.fetcher = fetcher; }
  async request(path, payload) {
    const response = await this.fetcher(CONNECTOR + '/api/manager/' + path, {
      method:payload === undefined ? 'GET' : 'POST', cache:'no-store',
      headers:{Authorization:'Bearer ' + this.token, ...(payload === undefined ? {} : {'Content-Type':'application/json'})},
      ...(payload === undefined ? {} : {body:JSON.stringify(payload)}),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'The local connector could not complete this request.');
    return result;
  }
  async run(action, payload = {}) {
    const {job} = await this.request(action, payload);
    for (;;) {
      await new Promise(resolve => setTimeout(resolve, 400));
      const state = await this.request('jobs/' + job);
      if (state.state === 'failed') throw new Error(state.error);
      if (state.state === 'complete') return state.result;
    }
  }
}

export async function withRegularMTP(client, action, payload = {}, enter = enterRegularMTP) {
  try { return await client.run(action, payload); } catch (error) {
    // These are pre-session refusals, before any file operation. A failure
    // during transfer is never retried automatically.
    if (!/Open regular COM|Expected one OP-XY in (?:MTP file transfer|file-transfer mode, found none)/.test(error.message)) throw error;
    await enter();
    return client.run(action, payload);
  }
}

export function changes(installed, selected, local, updates = new Set()) {
  const current = new Set(installed.map(m => m.id));
  return {remove:[...current].filter(id => !selected.has(id)),
    add:[...selected].filter(id => !current.has(id) && !local.has(id)),
    local:[...selected].filter(id => !current.has(id) && local.has(id)),
    update:[...updates].filter(id => current.has(id) && selected.has(id))};
}

export function installedVersion(machine) {
  const source = machine.source || {};
  if (source.tag) return source.tag;
  if (source.commit) return 'Commit ' + source.commit.slice(0, 8);
  return 'Saved build' + (source.fingerprint ? ' ' + source.fingerprint.slice(0, 8) : ' (version unknown)');
}

export function canSelectRelease(machine) { return machine?.compatibility?.state === 'sdk-compatible'; }

export function releaseAction(machine, candidate) {
  if (machine.source?.commit && machine.source.commit === candidate.commit) return 'Already installed';
  const version = tag => /^v?(\d+)\.(\d+)\.(\d+)$/.exec(tag || '')?.slice(1).map(Number);
  const before = version(machine.source?.tag), after = version(candidate.tag);
  if (before && after) {
    for (let i = 0; i < 3; i++) {
      if (after[i] < before[i]) return 'Switch to older ' + candidate.tag;
      if (after[i] > before[i]) break;
    }
  }
  return 'Update to ' + candidate.tag;
}

export function mount(root, {canWrite, back}) {
  const $ = name => root.querySelector('[data-manager="' + name + '"]');
  let client, installed = [], offered = [], selected = new Set(), updates = new Set(), locals = new Map();
  let plan = null, connected = false, busy = false, offline = false, revision = null;
  root.innerHTML = `<div class="page-header"><div><span class="eyebrow">Your OP–XY</span><h1 tabindex="-1">Manage machines</h1></div><button class="secondary" data-manager="back">Build firmware</button></div>
    <p>Add or remove machines through regular MTP, then reboot normally.</p>
    <section class="panel manager-connection"><h2>Connect your OP–XY</h2>
    <p>Start the Open-XY local connector, then open its pairing link or paste its pairing key. It builds source folders and handles verified transfers.</p>
    <details><summary>Start the connector</summary><p>In your matching SDK, run <code>python sdk.py manager --config &lt;your-manager-config.json&gt;</code>. The configuration keeps your verified kernel, collection and tool paths. Older firmware needs the collection loader first.</p></details>
    <label>Pairing key <input data-manager="token" type="password" autocomplete="off" spellcheck="false"></label>
    <div class="flow-actions"><button class="primary" data-manager="connect">Connect and read installed machines</button></div></section>
    <p data-manager="status" role="status" aria-live="polite">No device files have been changed.</p>
    <div data-manager="collection" hidden>
      <div class="manager-summary"><span data-manager="count"></span><button class="secondary" data-manager="refresh">Refresh installed collection</button></div>
      <section class="panel"><label>Catalog channel <select data-manager="channel"><option value="stable">Stable</option><option value="staging">Staging · experimental</option></select></label><p data-manager="catalog-info"></p></section>
      <div class="manager-columns"><section class="panel"><h2>Installed</h2><p>Uncheck to remove, or choose Update for a specific release. Project numbers and extensions are preserved.</p><div data-manager="installed" class="manager-installed"></div><p data-manager="extensions"></p></section>
      <section class="panel"><h2>Available machines</h2><label>Search catalog <input data-manager="search" type="search" placeholder="Machine name or ID"></label><div data-manager="catalog" class="manager-catalog"></div>
      <div class="manager-folder" data-manager="drop"><h3>Your own machines</h3><p>Drop a source folder here, or choose one. You can import a single machine or a folder containing several machines.</p><label class="file-button">Choose local folder<input data-manager="folder" type="file" webkitdirectory multiple class="sr-only"></label><div data-manager="local"></div></div></section></div>
      <div class="panel manager-review"><h2>Review changes</h2><p data-manager="changes"></p><button class="primary" data-manager="prepare">Build and check changes</button>
      <div data-manager="review" hidden><p data-manager="plan"></p><p data-manager="memory"></p><button class="primary" data-manager="apply">Apply changes to OP–XY</button></div></div>
    </div>`;
  const hash = /^#manage=([A-Za-z0-9_-]{20,128})$/.exec(location.hash);
  if (hash) { $('token').value = hash[1]; history.replaceState(null, '', location.pathname + location.search + '#manage'); }
  const message = (text, error = false) => { $('status').textContent = text; $('status').dataset.error = String(error); };
  const invalidate = () => { plan = null; $('review').hidden = true; updateChanges(); };
  function updateChanges() {
    const delta = changes(installed, selected, locals, updates);
    $('changes').textContent = `Add ${delta.add.length + delta.local.length} · Update ${delta.update.length} · Remove ${delta.remove.length}. Extensions stay installed.`;
    $('prepare').disabled = busy || !connected || !revision || !(delta.add.length || delta.local.length || delta.remove.length || delta.update.length);
  }
  async function operation(text, fn) {
    if (busy) return;
    busy = true; root.setAttribute('aria-busy', 'true');
    root.querySelectorAll('button,input,select').forEach(el => el.disabled = true);
    message(text);
    try { await fn(); } catch (error) { message(error.message || 'Cannot reach the local connector. Start it and open its pairing link.', true); }
    finally {
      busy = false; root.setAttribute('aria-busy', 'false');
      root.querySelectorAll('button,input,select').forEach(el => el.disabled = false);
      renderLists(); updateChanges();
      $('apply').disabled = !plan || !connected || offline || !canWrite();
    }
  }
  function row(machine, target, label = '', isInstalled = false) {
    const container = document.createElement('div'); container.className = 'manager-release';
    const line = document.createElement('label'); line.className = 'manager-machine';
    const box = document.createElement('input'); box.type = 'checkbox'; box.checked = selected.has(machine.id);
    box.disabled = !connected || !isInstalled && !canSelectRelease(machine);
    const text = document.createElement('span'); text.textContent = `${machine.name || machine.id} · ${machine.kind}${label ? ' · ' + label : ''}`;
    box.onchange = () => { box.checked ? selected.add(machine.id) : selected.delete(machine.id); if (!box.checked) updates.delete(machine.id); invalidate(); renderLists(); };
    line.append(box, text); container.append(line);
    const candidate = isInstalled ? offered.find(m => m.id === machine.id) : machine;
    if (candidate) {
      const detail = document.createElement('p'); detail.className = 'manager-release-detail';
      detail.textContent = `${isInstalled ? 'Available: ' + candidate.tag + ' · ' : ''}${candidate.experimental ? 'Experimental · ' : ''}${candidate.compatibility?.message || 'Compatibility not checked'}`;
      container.append(detail);
    }
    if (isInstalled && candidate) {
      const button = document.createElement('button'); button.className = 'secondary';
      const same = Boolean(machine.source?.commit && machine.source.commit === candidate.commit);
      button.textContent = updates.has(machine.id) ? 'Keep installed version' : releaseAction(machine, candidate);
      button.disabled = same || !connected || !selected.has(machine.id) || !canSelectRelease(candidate);
      button.onclick = () => { updates.has(machine.id) ? updates.delete(machine.id) : updates.add(machine.id); invalidate(); renderLists(); };
      container.append(button);
    }
    target.append(container);
  }
  function renderLists() {
    for (const name of ['installed', 'catalog', 'local']) $(name).replaceChildren();
    installed.forEach(m => row(m, $('installed'), 'Installed: ' + installedVersion(m), true));
    const ids = new Set(installed.map(m => m.id)), query = $('search').value.trim().toLowerCase();
    offered.filter(m => !ids.has(m.id) && !locals.has(m.id) && (m.id + ' ' + (m.name || '')).toLowerCase().includes(query))
      .forEach(m => row(m, $('catalog'), m.tag));
    locals.forEach(m => { if (!ids.has(m.id)) row(m, $('local'), 'local folder'); });
  }
  async function loadCatalog() {
    revision = null; offered = []; updates.clear(); invalidate();
    const current = new Set(installed.map(m => m.id));
    selected = new Set([...selected].filter(id => current.has(id) || locals.has(id)));
    const result = await client.run('catalog', {channel:$('channel').value});
    offered = result.machines; revision = result.revision;
    installed = installed.map(machine => result.installed_versions?.[machine.id]
      ? {...machine, source:{...machine.source, tag:result.installed_versions[machine.id]}} : machine);
    $('catalog-info').textContent = `${result.channel === 'staging' ? 'Staging releases are experimental.' : 'Stable published releases.'} Installed SDK ${result.sdk}. Catalog ${result.revision.slice(0, 8)}. Full build checks are required before applying.`;
    message(`${result.channel === 'staging' ? 'Staging' : 'Stable'} catalog ready. ${offline ? 'Saved collection preview; device access and transfers are disabled.' : 'Choose changes, then build and review before applying.'}`);
  }
  async function detect() {
    plan = null; connected = false; $('review').hidden = true;
    const session = await client.request('catalog'); offline = Boolean(session.offline);
    $('connect').textContent = offline ? 'Load saved collection preview' : 'Connect and read installed machines';
    $('refresh').textContent = offline ? 'Reload saved collection' : 'Refresh installed collection';
    const state = offline ? await client.run('status') : await withRegularMTP(client, 'status');
    installed = state.inventory?.machines || []; selected = new Set(installed.map(m => m.id)); updates.clear();
    connected = state.can_manage || state.can_prepare;
    $('collection').hidden = false;
    $('count').textContent = `${installed.length} machines · ${offline ? 'Saved collection preview · device not read' : state.attempt.state === 'confirmed' ? 'Startup confirmed' : state.attempt.state}`;
    $('extensions').textContent = 'Extensions kept: ' + (state.inventory?.extensions.join(', ') || 'none');
    await loadCatalog(); message(state.message);
  }
  $('connect').onclick = () => operation('Connecting and reading the installed collection…', async () => {
    if (!$('token').value.trim()) throw new Error('Open the pairing link printed by the local connector.');
    client = new Client($('token').value.trim());
    await client.request('catalog'); // Pair before sending any device command.
    await detect();
  });
  $('refresh').onclick = () => operation('Reading installed collection…', detect);
  $('back').onclick = back;
  $('search').oninput = renderLists;
  $('channel').onchange = () => operation('Checking the selected catalog against your installed SDK…', loadCatalog);
  async function importFiles(files) {
    if (!client || !connected) throw new Error('Connect and verify your installed collection first.');
    invalidate();
    const result = await client.run('import', {files:await encodeFolder(files)});
    result.machines.forEach(m => { if (installed.some(existing => existing.id === m.id)) throw new Error(m.id + ' is already installed. Existing versions are preserved.'); locals.set(m.id, m); selected.add(m.id); });
    message('Local source imported. Build and check changes before applying.');
  }
  $('folder').onchange = event => { const files = [...event.target.files]; event.target.value = ''; operation('Importing local machine sources…', () => importFiles(files)); };
  for (const type of ['dragover', 'dragleave', 'drop']) $('drop').addEventListener(type, event => {
    event.preventDefault(); $('drop').dataset.drag = String(type === 'dragover');
    if (type === 'drop' && !busy) operation('Reading local source folder…', async () => {
      const files = [];
      async function walk(entry, prefix = '') {
        if (SKIP.has(entry.name)) return;
        if (entry.isFile) {
          const file = await new Promise((resolve, reject) => entry.file(resolve, reject));
          Object.defineProperty(file, 'webkitRelativePath', {value:prefix + entry.name}); files.push(file);
          sourceFilesBound();
        } else if (entry.isDirectory) {
          const reader = entry.createReader();
          for (;;) {
            const entries = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
            if (!entries.length) break;
            for (const child of entries) await walk(child, prefix + entry.name + '/');
          }
        }
      }
      function sourceFilesBound() {
        if (files.length > 4096 || files.reduce((sum, f) => sum + f.size, 0) > MAX_SOURCE_BYTES) throw new Error('Source folder exceeds the import limit.');
      }
      const entries = [...event.dataTransfer.items].map(item => item.webkitGetAsEntry?.()).filter(Boolean);
      if (!entries.length) throw new Error('Use Choose local folder in this browser.');
      for (const entry of entries) await walk(entry);
      await importFiles(files);
    });
  });
  $('prepare').onclick = () => operation('Building and checking your collection. Your OP–XY is unchanged…', async () => {
    plan = null; $('review').hidden = true;
    const result = await client.run('prepare', {...changes(installed, selected, locals, updates), channel:$('channel').value, catalog_revision:revision});
    plan = result.plan;
    $('plan').textContent = `Add: ${result.add.join(', ') || 'none'}. Update: ${(result.update || []).map(m => m.id + ': ' + m.before + ' → ' + m.after + (m.notes.length ? ' (' + m.notes.join('; ') + ')' : '')).join(', ') || 'none'}. Remove: ${result.remove.join(', ') || 'none'}. ${result.machines.length} machines will remain installed.`;
    $('memory').textContent = `${(result.sizes.allocated_span_bytes / 1024).toFixed(1)} KiB used. Existing machine numbers and extensions verified.`;
    $('review').hidden = false; message(result.message + (offline ? ' Offline preview: no device connection or transfer is allowed.' : canWrite() ? '' : ' Apply is disabled in preview; open the manager normally to install.'));
  });
  $('apply').onclick = () => operation('Transferring and verifying the staged collection. Keep USB connected…', async () => {
    if (!canWrite()) throw new Error('Acknowledge the installation notice before applying changes.');
    if (offline) throw new Error('Offline preview cannot access the device or apply changes.');
    const applying = plan; plan = null; $('review').hidden = true;
    const result = await withRegularMTP(client, 'apply', {plan:applying});
    connected = false;
    message(`Collection ${result.generation} staged and verified. Reboot normally, wait at least 25 seconds after the normal screen appears, then refresh to check startup confirmation. No firmware was flashed.`);
  });
  updateChanges();
}
