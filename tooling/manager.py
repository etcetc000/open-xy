"""Local connector for the web collection manager. No firmware flashing endpoints."""
import argparse
import base64
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
import os
from pathlib import Path, PurePosixPath
import secrets
import shutil
import subprocess
import sys
import threading
from urllib.parse import urlsplit

from . import catalog, collection_bundle, collection_manifest as inventory, device
from .formats import require, sha
from .pipeline import ROOT, document, fresh_output, save_json
from .release import outside_checkouts

PORT = 8920
MAX_UPLOAD = 64 * 1024 * 1024
IGNORED = {'.git', '__pycache__', 'node_modules', '.venv', '.DS_Store'}
ORIGINS = {'https://open-xy.com', 'https://beta.open-xy.com',
           'http://127.0.0.1:8918', 'http://localhost:8918',
           'http://127.0.0.1:8919', 'http://localhost:8919', f'http://127.0.0.1:{PORT}'}


def source_path(name):
    require(isinstance(name, str) and 0 < len(name) <= 512 and '\\' not in name and
            ':' not in name and '\0' not in name, 'Invalid source file path')
    parts = name.split('/')
    require(all(p not in ('', '.', '..') for p in parts) and not PurePosixPath(name).is_absolute(),
            'Source files must stay inside the imported folder')
    return parts


def identity_at(root):
    definition = document(Path(root) / 'definition.json')
    metadata = document(Path(root) / 'machine.json')
    name = definition.get('id')
    require(isinstance(name, str) and name.isascii() and name.replace('_', '').isalnum() and
            name[0].islower() and len(name) <= 24, 'Invalid local machine ID')
    return dict(id=name, name=definition.get('name', name), kind=metadata['kind'])


def collection_entry(connected, name):
    listings = device.Listings(connected)
    parent = listings.resolve(connected.root(), ('projects', 'openxy'))
    require(parent is not None, 'No collection loader on this device')
    return listings.child(parent, name)


def same_snapshot(a, b):
    return a['kernel'] == b['kernel'] and a['selection'] == b['selection'] and a['attempt'] == b['attempt']


def source_hashes(report, name, extension=False):
    prefix = ('extension:' if extension else 'external/') + name + '/'
    found = {k: v for k, v in report['sources'].items() if k.startswith(prefix)}
    require(found, 'Missing build provenance for ' + name)
    return found


class Manager:
    def __init__(self, config, *, connect=device.open_device):
        self.config_path = Path(config).resolve()
        self.config = document(self.config_path)
        for key in ('sdk', 'kernel_package', 'current_collection', 'python', 'toolchain', 'compiler'):
            require(isinstance(self.config.get(key), str) and Path(self.config[key]).exists(), 'Missing manager configuration: ' + key)
        self.home = device.private_root() / 'manager'
        require(outside_checkouts(self.home), 'Manager data must live outside every checkout')
        self.home.mkdir(parents=True, exist_ok=True)
        self.state_path = self.home / (sha(str(self.config_path).encode())[:16] + '.json')
        self.known = {}
        for path in [self.config['current_collection'], *document(self.state_path).get('bundles', [])] if self.state_path.exists() else [self.config['current_collection']]:
            description, _, _, _ = collection_bundle.read_bundle(path)
            self.known[description['digest']] = str(Path(path).resolve())
        self.connect = connect
        self.current = None
        self.local = {}
        self.prepared = {}
        self.jobs = {}
        self.lock = threading.Lock()
        self.entries = {e['id']: e for e in catalog.load() if e['status'] == 'published'}

    def read_status(self):
        with self.connect() as connected:
            raw, info, _, _, _, _ = device.collection_records(connected)
            status = device.collection_status(connected)
            status['kernel_sha256'] = sha(raw)
            selected = status['selection']
            digest = selected.get('digest') if status['next_boot'] == 'external' else None
            status['inventory'] = None
            if digest:
                entry = collection_entry(connected, inventory.filename(digest))
                if entry is not None:
                    require(not entry.folder and entry.size <= inventory.LIMIT, 'Invalid device inventory')
                    status['inventory'] = inventory.decode(connected.read(entry), digest=digest, kernel=sha(raw))
            bundle = self.known.get(digest)
            status['can_manage'] = bool(bundle and status['next_boot'] == 'external' and
                                        status['attempt'].get('state') == 'confirmed' and
                                        status['attempt'].get('generation') == selected.get('generation') and
                                        status['attempt'].get('digest') == digest and
                                        status['attempt'].get('slot') == selected.get('slot'))
            if bundle:
                description, _, _, _ = collection_bundle.read_bundle(bundle)
                require(description['kernel'] == sha(raw), 'Saved collection belongs to another kernel')
                slot = collection_entry(connected, 'slot' + str(selected['slot']) + '.oxc')
                require(slot is not None and not slot.folder and slot.size == (Path(bundle) / 'bundle.oxc').stat().st_size,
                        'Active collection file is missing or has changed')
                require(sha(connected.read(slot)) == description['image_sha256'], 'Active collection differs from its saved build')
                expected = self.describe_bundle(bundle)
                if status['inventory'] is not None:
                    require(status['inventory'] == expected, 'Device inventory differs from its verified build')
                status['inventory'] = expected
            status['message'] = ('Installed collection verified. Choose machines to add or remove.' if status['can_manage'] else
                                 'This collection cannot be safely rebuilt here. Use its matching kernel package and saved collection; reboot and confirm any pending update first.')
        self.current = copy.deepcopy(status)
        return status

    def describe_bundle(self, folder, origins=None):
        folder = Path(folder)
        description, _, _, _ = collection_bundle.read_bundle(folder)
        if origins is None and (folder / 'inventory.oxc').is_file():
            metadata=inventory.decode((folder / 'inventory.oxc').read_bytes(),digest=description['digest'],kernel=description['kernel'])
            require([m['id'] for m in metadata['machines']]==description['linked'] and
                    metadata['layout']==document(folder/'layout.json')['machines'],'Saved inventory differs from its build')
            return metadata
        layout = document(folder / 'layout.json')['machines']
        report = document(folder / 'analysis/compile-report.json')
        machines, extensions = [], []
        for entry in description['collection']:
            if 'extension' in entry:
                extensions.append(document(Path(entry['extension']) / 'extension.json')['id'])
            else:
                m = identity_at(entry['path'])
                m['identity'] = layout[m['id']]
                m['source'] = (origins or {}).get(m['id'], dict(type='installed', fingerprint=sha(
                    json.dumps(source_hashes(report, m['id']), sort_keys=True).encode())))
                machines.append(m)
        return inventory.decode(inventory.encode(description, layout, machines, extensions))

    def catalog_list(self):
        return [dict(id=e['id'], kind=e['kind'], tag=e['tag'], maintainer=e['maintainer'], repository=e['repository'])
                for e in self.entries.values()]

    def import_folder(self, payload):
        files = payload.get('files')
        require(isinstance(files, list) and 0 < len(files) <= 4096, 'Choose a machine source folder (at most 4096 files)')
        total, seen, decoded = 0, set(), []
        for item in files:
            parts = source_path(item['path'])
            require(not any(p in IGNORED for p in parts), 'Exclude version control, environments and caches from the source folder')
            key = '/'.join(parts).casefold()
            require(key not in seen, 'Duplicate source file path')
            seen.add(key)
            data = base64.b64decode(item['data'], validate=True)
            total += len(data)
            require(total <= MAX_UPLOAD, 'Source folder exceeds 64 MiB')
            decoded.append((parts, data))
        folder = fresh_output(self.home / ('import-' + secrets.token_hex(8)))
        for parts, data in decoded:
            path = folder.joinpath(*parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        found = []
        for path in sorted(folder.rglob('machine.json')):
            m = identity_at(path.parent)
            require(m['id'] not in self.local, 'That local machine is already imported: ' + m['id'])
            require(not self.current or m['id'] not in {x['id'] for x in self.current['inventory']['machines']},
                    'Existing machines keep their current sources: ' + m['id'])
            found.append((m, path.parent))
        require(found and len({m['id'] for m, _ in found}) == len(found), 'Choose a folder with distinct machine.json and definition.json pairs')
        for m, root in found:
            self.local[m['id']] = dict(path=str(root), metadata=m)
        return dict(machines=[m for m, _ in found])

    def _snapshot(self, root, target):
        root = Path(root).resolve()
        target.mkdir(parents=True)
        for folder, dirs, files in os.walk(root):
            dirs[:] = sorted(d for d in dirs if d not in IGNORED)
            here = Path(folder)
            require(not any(device.link(here / d) for d in dirs), 'Source folders cannot contain links')
            for name in files:
                if name in IGNORED:
                    continue
                source = here / name
                require(not device.link(source), 'Source files cannot be links')
                destination = target / source.relative_to(root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
        return str(target)

    def prepare(self, payload):
        require(self.current and self.current['can_manage'], 'Connect and verify the installed collection first')
        before = copy.deepcopy(self.current)
        remove, add, local = (payload.get(k, []) for k in ('remove', 'add', 'local'))
        require(all(isinstance(v, list) and all(isinstance(x, str) for x in v) and len(v) == len(set(v))
                    for v in (remove, add, local)), 'Invalid machine selection')
        installed = {m['id'] for m in before['inventory']['machines']}
        require(set(remove) <= installed and set(add) <= self.entries.keys() and set(local) <= self.local.keys(), 'Unknown machine selection')
        require(not ((set(add) | set(local)) & installed) and not (set(add) & set(local)),
                'Existing machines keep their exact versions; remove and install separately to replace one')
        require(remove or add or local, 'Choose at least one change')
        require((installed - set(remove)) | set(add) | set(local), 'Keep at least one machine')
        previous = Path(self.known[before['selection']['digest']])
        old = document(previous / 'collection.json')
        baseline = document(previous / 'analysis/compile-report.json')
        key = secrets.token_hex(12)
        run = fresh_output(self.home / ('build-' + key))
        entries, origins = [], {m['id']: m['source'] for m in before['inventory']['machines']}
        for entry in old['collection']:
            field = 'extension' if 'extension' in entry else 'path'
            name = document(Path(entry[field]) / ('extension.json' if field == 'extension' else 'definition.json'))['id']
            if field == 'path' and name in remove:
                continue
            entries.append({field: self._snapshot(entry[field], run / 'sources' / (field + '-' + name))})
        for name in add:
            entry = self.entries[name]
            machine = catalog.fetch(entry, self.home / 'catalog')
            entries.append(dict(path=self._snapshot(machine.root, run / 'sources' / ('path-' + name))))
            origins[name] = {k: entry[k] for k in ('repository', 'commit', 'tag', 'maintainer')}
            origins[name]['type'] = 'catalog'
        for name in local:
            entries.append(dict(path=self._snapshot(self.local[name]['path'], run / 'sources' / ('path-' + name))))
            origins[name] = dict(type='local')
        collection = run / 'selection.json'
        save_json(collection, dict(schema=1, machines=[dict(path=e['path']) for e in entries if 'path' in e],
                                   extensions=[dict(path=e['extension']) for e in entries if 'extension' in e]))
        output = run / 'bundle'
        command = [self.config['python'], str(Path(self.config['sdk']) / 'sdk.py'), 'collection', 'executable',
                   '--kernel', self.config['kernel_package'], '--previous', str(previous), '--collection', str(collection),
                   '--out', str(output), '--toolchain', self.config['toolchain'], '--compiler', self.config['compiler']]
        with (run / 'build.log').open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
        require(result.returncode == 0, 'Collection build failed; nothing was transferred. ' +
                (run / 'build.log').read_text(errors='replace')[-2200:])
        description, _, _, _ = collection_bundle.read_bundle(output)
        report = document(output / 'analysis/compile-report.json')
        for name in installed - set(remove):
            require(source_hashes(report, name) == source_hashes(baseline, name), 'An existing machine source changed: ' + name)
        require(report['linked'] == [m['id'] for m in self.describe_bundle(output)['machines']], 'Build selection changed')
        for name in before['inventory']['extensions']:
            require(source_hashes(report, name, True) == source_hashes(baseline, name, True), 'An existing extension source changed: ' + name)
        old_layout = document(previous / 'layout.json')['machines']
        layout = document(output / 'layout.json')['machines']
        require(all(layout.get(k) == v for k, v in old_layout.items()), 'Existing machine numbers or removed ID reservations changed')
        metadata = self.describe_bundle(output, origins)
        data = inventory.encode(description, layout, metadata['machines'], metadata['extensions'])
        (output / 'inventory.oxc').write_bytes(data)
        self.prepared[key] = dict(bundle=str(output), before=before, inventory=data, description=description)
        return dict(plan=key, add=add + local, remove=remove, machines=metadata['machines'],
                    extensions=metadata['extensions'], sizes=report['sizes'], digest=description['digest'],
                    message='Build verified. Apply stages this collection for the next normal reboot; no firmware is flashed. Existing presets are kept; new factory presets are not installed.')

    def apply(self, payload):
        key = payload.get('plan')
        require(key in self.prepared, 'Prepare and review a collection first')
        plan = self.prepared[key]
        # Persist the matching host build before selection can change on the device.
        bundles = set(self.known.values()) | {plan['bundle']}
        temporary = self.state_path.with_suffix('.tmp')
        save_json(temporary, dict(bundles=sorted(bundles)))
        temporary.replace(self.state_path)
        self.known[plan['description']['digest']] = plan['bundle']
        record = self.home / ('transfer-' + secrets.token_hex(12))
        with self.connect() as connected:
            now = device.collection_status(connected)
            require(same_snapshot(now, plan['before']), 'The device changed after detection. Reconnect and prepare again; nothing was transferred.')
            slot = collection_entry(connected, 'slot' + str(now['selection']['slot']) + '.oxc')
            previous = document(Path(self.known[now['selection']['digest']]) / 'collection.json')
            require(slot is not None and not slot.folder and slot.size == (Path(self.known[now['selection']['digest']])/'bundle.oxc').stat().st_size and
                    sha(connected.read(slot)) == previous['image_sha256'],
                    'The installed payload changed after detection; nothing was transferred')
            result = device.install_collection(connected, plan['bundle'], record, inventory=plan['inventory'], skip_presets=True)
        self.prepared.clear()
        self.current = None
        return result

    def start(self, action, payload):
        require(action in ('status', 'import', 'prepare', 'apply'), 'Unknown manager action')
        require(self.lock.acquire(blocking=False), 'Another manager operation is running')
        key = secrets.token_hex(12)
        self.jobs[key] = dict(state='running', action=action)
        def run():
            try:
                fn = {'status': self.read_status, 'import': self.import_folder,
                      'prepare': self.prepare, 'apply': self.apply}[action]
                result = fn() if action == 'status' else fn(payload)
                self.jobs[key] = dict(state='complete', action=action, result=result)
            except Exception as error:
                self.jobs[key] = dict(state='failed', action=action, error=str(error))
            finally:
                self.lock.release()
        threading.Thread(target=run, daemon=True).start()
        return dict(job=key)


def handler(manager, token, site):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def respond(self, status, data):
            raw = json.dumps(data).encode()
            self.send_response(status)
            origin = self.headers.get('Origin')
            if origin in ORIGINS:
                self.send_header('Access-Control-Allow-Origin', origin)
                self.send_header('Vary', 'Origin')
            self.send_header('Access-Control-Allow-Headers', 'Authorization, Content-Type')
            self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Type', 'application/json')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def request(self):
            try:
                require(self.headers.get('Host') == f'127.0.0.1:{self.server.server_port}', 'Invalid connector host')
                path = urlsplit(self.path).path
                if path.startswith('/api/manager/'):
                    origin = self.headers.get('Origin')
                    require(origin is None or origin in ORIGINS, 'This website is not allowed to access the connector')
                    if self.command == 'OPTIONS':
                        self.respond(200, {})
                        return
                    require(secrets.compare_digest(self.headers.get('Authorization', ''), 'Bearer ' + token), 'Pair with the local connector first')
                    action = path.removeprefix('/api/manager/')
                    if self.command == 'GET':
                        if action == 'catalog':
                            self.respond(200, dict(machines=manager.catalog_list()))
                        elif action.startswith('jobs/') and action[5:] in manager.jobs:
                            self.respond(200, manager.jobs[action[5:]])
                        else:
                            self.respond(404, dict(error='Unknown manager endpoint'))
                        return
                    require(self.command == 'POST', 'Unsupported manager method')
                    require(self.headers.get('Content-Type') == 'application/json', 'Send JSON to the connector')
                    length = int(self.headers.get('Content-Length', '0'))
                    require(0 < length <= MAX_UPLOAD * 2, 'Manager request exceeds its size limit')
                    payload = json.loads(self.rfile.read(length))
                    require(isinstance(payload, dict), 'Invalid manager request')
                    self.respond(202, manager.start(action, payload))
                    return
                require(self.command == 'GET' and site is not None, 'Unknown connector endpoint')
                require(path in ('/', '/index.html') or path.startswith('/assets/'), 'Unknown connector page')
                parts = source_path(path.lstrip('/') or 'index.html')
                file = (site / '/'.join(parts)).resolve()
                require(file.is_relative_to(site) and file.is_file(), 'Unknown connector page')
                raw = file.read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', mimetypes.guess_type(file)[0] or 'application/octet-stream')
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Frame-Options', 'DENY')
                self.send_header('Referrer-Policy', 'no-referrer')
                self.send_header('X-Content-Type-Options', 'nosniff')
                self.send_header('Content-Length', str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
            except (ValueError, OSError, KeyError, TypeError) as error:
                self.respond(400, dict(error=str(error)))

        do_GET = do_POST = do_OPTIONS = request
    return Handler


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True, help='Matching SDK, kernel package, current collection and pinned tool paths')
    parser.add_argument('--site', type=Path, help='Previously built web UI; otherwise builds a fresh UI outside the checkout')
    args = parser.parse_args(argv)
    manager = Manager(args.config)
    site = args.site.resolve() if args.site else manager.home / ('web-' + secrets.token_hex(8))
    if not args.site:
        env = dict(os.environ, OPEN_XY_KIT='', OPEN_XY_OUT_DIR=str(site))
        subprocess.run(['npm', 'run', 'build'], cwd=ROOT, env=env, check=True)
    require(site.is_dir() and (site / 'index.html').is_file() and outside_checkouts(site), 'Choose an external web UI build directory')
    token = secrets.token_urlsafe(32)
    server = ThreadingHTTPServer(('127.0.0.1', PORT), handler(manager, token, site))
    server.daemon_threads = True
    print(f'Open http://127.0.0.1:{PORT}/#manage={token}', flush=True)
    print('Local connector ready. No device commands are sent until you click Connect or Apply in the web UI.', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        # Finish an in-flight checked transfer before the connector process exits.
        with manager.lock:
            pass
    return 0
