"""Pinned public catalogs and advisory compatibility for the local manager."""
import ast
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
from urllib.parse import quote
from urllib.request import urlopen

from . import catalog
from .formats import require, sha

CHANNELS = {'stable': 'main', 'staging': 'staging'}
UPSTREAM = 'https://github.com/etcetc000/open-xy'


def public_json(url):
    with urlopen(url, timeout=15) as response:
        data = response.read(2 * 1024 * 1024 + 1)
    require(len(data) <= 2 * 1024 * 1024, 'Catalog metadata exceeds its limit')
    return json.loads(data)


def installed_sdk(root):
    tree = ast.parse((Path(root) / 'tooling/machines.py').read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'SDK_VERSION' for t in node.targets):
            version = ast.literal_eval(node.value)
            require(isinstance(version, tuple) and len(version) == 2 and all(type(n) is int for n in version),
                    'Invalid installed SDK version')
            return version
    raise ValueError('The matching SDK does not declare its version')


def saved_origin(root, name, fingerprints):
    origin = dict(type='installed', fingerprint=sha(json.dumps(fingerprints, sort_keys=True).encode()))
    commit = catalog.head(root)
    if commit and fingerprints and catalog.untouched(root):
        prefix = 'external/' + name + '/'
        if all(key.startswith(prefix) and (Path(root) / key[len(prefix):]).is_file() and
               sha((Path(root) / key[len(prefix):]).read_bytes()) == digest for key, digest in fingerprints.items()):
            origin['commit'] = commit
    return origin


def compatibility(metadata, version, services):
    sdk = metadata.get('sdk')
    if not isinstance(sdk, str) or not re.fullmatch(r'\d+\.\d+', sdk):
        return dict(state='unavailable', message='Missing or invalid SDK declaration', required_sdk=sdk)
    required = tuple(map(int, sdk.split('.')))
    kind = metadata.get('kind')
    service = {'drum': 'drums', 'sample': 'samples', 'player': 'players'}.get(kind)
    if required[0] != version[0] or required[1] > version[1] or service and service not in services:
        return dict(state='firmware-update-required', message=f'Requires SDK {sdk} and {kind} support in firmware', required_sdk=sdk)
    if kind not in ('synth', 'sample', 'drum', 'effect', 'player'):
        return dict(state='unavailable', message='Unsupported machine kind', required_sdk=sdk)
    return dict(state='sdk-compatible', message=f'SDK {sdk} compatible · full build required', required_sdk=sdk)


class Releases:
    def __init__(self, sdk, services, *, read=public_json, resolve=None):
        self.version = installed_sdk(sdk)
        self.services = set(services)
        self.read = read
        self.resolve = resolve or self.remote_revision
        self.snapshots = {}
        self.active = None
        self.metadata = {}

    @staticmethod
    def remote_revision(branch):
        lines = catalog.git('ls-remote', UPSTREAM, 'refs/heads/' + branch).splitlines()
        require(len(lines) == 1, 'Cannot resolve the catalog channel')
        revision, ref = lines[0].split('\t')
        require(ref == 'refs/heads/' + branch and re.fullmatch('[0-9a-f]{40}', revision), 'Invalid catalog revision')
        return revision

    def inspect(self, entry):
        key = (entry['repository'], entry['commit'], entry['path'])
        if key not in self.metadata:
            repository = entry['repository'].removeprefix('https://github.com/')
            path = '/'.join(quote(p, safe='') for p in Path(entry['path']).parts if p != '.')
            url = f'https://raw.githubusercontent.com/{repository}/{entry["commit"]}/' + (path + '/' if path else '') + 'machine.json'
            try:
                metadata = self.read(url)
                require(isinstance(metadata, dict) and metadata.get('kind') == entry['kind'], 'Catalog kind differs from its source')
                self.metadata[key] = compatibility(metadata, self.version, self.services)
            except Exception as error:
                self.metadata[key] = dict(state='unavailable', message='Could not check source metadata: ' + str(error), required_sdk=None)
        return self.metadata[key]

    def select(self, channel):
        require(channel in CHANNELS, 'Choose Stable or Staging')
        if channel not in self.snapshots:
            revision = self.resolve(CHANNELS[channel])
            require(isinstance(revision, str) and re.fullmatch('[0-9a-f]{40}', revision), 'Invalid catalog revision')
            entries = catalog.validate(self.read(f'https://raw.githubusercontent.com/etcetc000/open-xy/{revision}/catalog/machines.json'))
            entries = [e for e in entries if e['status'] == 'published' or channel == 'staging' and e['status'] == 'draft']
            with ThreadPoolExecutor(max_workers=6) as pool:
                checks = list(pool.map(self.inspect, entries))
            self.snapshots[channel] = dict(channel=channel, revision=revision,
                sdk='.'.join(map(str, self.version)), machines=[dict(e, compatibility=c, experimental=channel == 'staging')
                    for e, c in zip(entries, checks)])
        self.active = self.snapshots[channel]
        return self.active

    def selected(self, payload):
        require(self.active is not None and payload.get('channel') == self.active['channel'] and
                payload.get('catalog_revision') == self.active['revision'], 'Catalog changed; reload and review your selection')
        return {e['id']: e for e in self.active['machines']}


def unchanged_controls(old_root, new_root):
    old = json.loads((Path(old_root) / 'definition.json').read_text())
    new = json.loads((Path(new_root) / 'definition.json').read_text())
    require(old['id'] == new['id'], 'An update must retain the machine ID')
    before = json.loads((Path(old_root) / 'machine.json').read_text())
    after = json.loads((Path(new_root) / 'machine.json').read_text())
    require(before['kind'] == after['kind'] and before.get('base') == after.get('base'), 'An update must retain its kind and sample base')
    changed, notes = catalog.control_changes(old.get('parameters', []), new.get('parameters', []))
    require(not changed, 'Update would reinterpret saved controls: ' + '; '.join(changed))
    for key in ('modelCount', 'padSettings', 'sliceTiming'):
        require(old.get('resources', {}).get(key) == new.get('resources', {}).get(key),
                'Update changes saved drum/sample settings: ' + key)
    return notes
