"""Machine discovery: each machine folder's machine.json declares what the SDK builds.

A machine lives in machines/<id>/ or in a folder of its own anywhere, such as
its own repository. Its definition's id is its stock name: the engine type the
device lists, its catalog and preset entries, its screen file and its symbol
prefix. machine.json declares the kind, the SDK version it targets, stock IDs
and build inputs. Saved presets and Open-XY layouts refer to the name and IDs,
so they never change once published; the SDK checks them against the stock
tables and the collection.
"""
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re

from .definitions import ANALYSIS_HEAP_LIMIT, ROOT, stock_name, validate_definition
from .formats import require
from .pipeline import config

KINDS = ('synth', 'sample', 'effect')
FIELDS = {'schema', 'sdk', 'kind', 'description', 'identity', 'sources', 'includes', 'icon', 'presetDefaults',
          'dryParameter'}
# The public machine contract: headers, definition schema, artwork rules and
# callbacks. A machine names the version it was written for; the SDK builds a
# machine of the same major version and an equal or older minor version.
SDK_VERSION = (1, 0)
# Builder card glyph: SVG path data in a 46 x 48 box, drawn as a stroke.
ICON = re.compile(r'[MLHVCSQTAZmlhvcsqtaz][MLHVCSQTAZmlhvcsqtaz0-9 .,-]{0,511}')
COLLECTION = ROOT / 'profiles/collection.json'
# Folders inside a machine that are never its source: version control, caches, tools.
IGNORED = {'.git', '__pycache__', 'node_modules', '.venv'}


def tree(root):
    """Every file under a machine folder, in name order, without walking ignored folders."""
    found = []
    for folder, folders, files in os.walk(root):
        folders[:] = sorted(f for f in folders if f not in IGNORED)
        found += [Path(folder) / name for name in files]
    return sorted(found)


@dataclass(frozen=True)
class Machine:
    id: str
    kind: str
    name: str
    description: str
    display: str
    sources: tuple
    includes: tuple
    definition: dict
    identity: dict
    preset_defaults: dict
    root: Path = None
    icon: str = ''
    dry_parameter: int | None = None

    # The stock name and the symbol prefix are the machine's name.
    @property
    def native_name(self):
        return self.id

    @property
    def prefix(self):
        return self.id

    @property
    def bundled(self):
        """A machine of this repository's machines/ folder, rather than a folder elsewhere."""
        return self.root == ROOT / 'machines' / self.id

    def source(self, relative=''):
        """A machine file as the build names it: machines/<id>/... for a bundled machine, so
        rebuilds stay byte-identical, and an absolute path for a machine elsewhere."""
        base = 'machines/' + self.id if self.bundled else self.root.as_posix()
        return base + ('/' + relative if relative else '')

    def files(self):
        """Every authored file, keyed as the build's provenance names it."""
        key = 'machines/' + self.id if self.bundled else 'external/' + self.id
        return {key + '/' + p.relative_to(self.root).as_posix(): p for p in tree(self.root) if p.suffix != '.md'}

    def contains(self, path):
        """Whether a path lies inside this machine's folder, where build output must never go."""
        return Path(os.path.abspath(path)).is_relative_to(self.root)


def _relative(machine_root, value):
    require(isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9_./-]+', value) and
            '..' not in value.split('/') and not value.startswith('/'), 'Invalid machine path: ' + str(value))
    path = (machine_root / value).resolve()
    require(path.is_relative_to(machine_root.resolve()) and path.exists(), 'Missing machine path: ' + value)
    return value


def check_identity(rules, kind, value):
    """IDs a machine may claim under the profile's identity rules (the browser builder applies the same rules).

    Engines take a free entry of the relocated table, never the stock "none" value; a
    synth draws its own page above the stock "none" page, a sample engine an ID only
    the relocated table holds and a stock sample page, an effect a free FX ID on the FX page.
    """
    require(isinstance(value, dict), 'machine.json needs an identity')
    slot = 'effect' if kind == 'effect' else 'engine'
    require(set(value) == {slot, 'page'}, 'Unexpected identity fields')
    number, page = value[slot], value['page']
    require(type(number) is int and type(page) is int, 'IDs must be integers')
    if kind == 'effect':
        require(number in rules['effects'] and page == rules['effect_page'],
                'Effects use an FX ID stock leaves unregistered, on the stock FX page')
        return {slot: number, 'page': page}
    require(0 < number < rules['engines'] and number != rules['engine_none'] and number not in rules['stock_engines'],
            'Engine ID outside the relocated table, the stock "none" value or registered by stock')
    if kind == 'sample':
        require(rules['engine_none'] < number and page in rules['sample_pages'],
                'Sample engines need a relocated-table engine ID and a stock sample page')
    else:
        require(rules['page_none'] < page < rules['pages'], 'A machine with its own screen needs a relocated page')
    return {slot: number, 'page': page}


def sdk_version(text):
    """A machine's declared SDK version, checked against this SDK."""
    require(isinstance(text, str) and re.fullmatch(r'\d+\.\d+', text), 'machine.json needs "sdk": "<major>.<minor>"')
    major, minor = map(int, text.split('.'))
    current = '%d.%d' % SDK_VERSION
    require(major == SDK_VERSION[0] and minor <= SDK_VERSION[1],
            f'The machine targets SDK {text}; this SDK is {current} and builds {SDK_VERSION[0]}.0 to {current}')
    return text


def machine(identity):
    """A bundled machine, machines/<identity>/."""
    stock_name(identity)
    return machine_at(ROOT / 'machines' / identity, identity)


def machine_at(root, identity=None):
    """The machine in a folder; its definition names it."""
    root = Path(os.path.abspath(root))
    path = root / 'machine.json'
    require(path.is_file() and path.resolve() == path.absolute() and root.resolve() == root,
            'Unknown or redirected machine: ' + str(identity or root))
    value = json.loads(path.read_text(encoding='utf-8'))
    if identity is None:
        named = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
        identity = named.get('id') if isinstance(named, dict) else None
        stock_name(identity)
    require(isinstance(value, dict) and value.get('schema') == 1 and
            set(value) <= FIELDS,
            'Unexpected machine.json fields: ' + identity)
    sdk_version(value.get('sdk'))
    kind = value.get('kind')
    require(kind in KINDS, 'Unknown machine kind: ' + str(kind))
    require(isinstance(value.get('description'), str) and 0 < len(value['description']) <= 160,
            'Machine needs a short description')
    sources = tuple(_relative(root, s) for s in value.get('sources', ()))
    includes = tuple(_relative(root, s) for s in value.get('includes', ()))
    require(all(s.endswith(('.cc', '.cpp')) for s in sources), 'Extra sources must be C++ translation units')
    icon = value.get('icon', '')
    require(icon == '' or isinstance(icon, str) and ICON.fullmatch(icon), 'icon is SVG path data')
    definition = validate_definition(json.loads((root / 'definition.json').read_text(encoding='utf-8')), kind)
    require(definition['id'] == identity, 'Machine directory and definition identity disagree')
    require(root == ROOT / 'machines' / identity or not (ROOT / 'machines' / identity).exists(),
            f'A machine named {identity} is bundled with the SDK; choose another ID')
    dry_parameter = None
    if 'dryParameter' in value:
        require(kind == 'effect' and isinstance(value['dryParameter'], str),
                'dryParameter names a normalized control on an effect')
        matches = [i for i, p in enumerate(definition['parameters']) if p['id'] == value['dryParameter']]
        require(len(matches) == 1, 'dryParameter must name a declared control')
        dry_parameter = matches[0]
        require(definition['parameters'][dry_parameter]['mapping'] ==
                dict(kind='linear', minimum=0, maximum=1), 'dryParameter must map linearly from 0 to 1')
    # A synth draws its own screen; sample engines and effects use a stock page.
    display = 'drawing' if kind == 'synth' else 'stock'
    # The native plan compiles extra translation units only into synths.
    if kind != 'synth':
        require(not sources and not includes, 'Sample engines and effects compile one fx.cpp or sample.cpp')
    # Pad settings live in the stock drum page's pad records, whose fade the SDK then owns.
    if kind == 'sample' and definition['resources'].get('padSettings', 0):
        require(value['identity']['page'] == 7, 'Pad settings and drum controls need stock page 7')
    defaults = value.get('presetDefaults', {})
    require(isinstance(defaults, dict) and set(defaults) <= {'amp'}, 'Invalid preset defaults')
    amp = defaults.get('amp', {})
    require(isinstance(amp, dict) and set(amp) <= {'attack', 'decay', 'release', 'sustain'} and
            all(type(v) is int and 0 <= v <= 32767 for v in amp.values()), 'Invalid preset amp defaults')
    return Machine(id=identity, kind=kind, name=definition['name'], description=value['description'],
                   display=display, sources=sources, includes=includes,
                   definition=definition,
                   identity=check_identity(config()['identities'], kind, value['identity']), preset_defaults=defaults,
                   root=root, icon=icon, dry_parameter=dry_parameter)


def discovered():
    """Every machine folder in the repository, in name order."""
    return [machine(path.parent.name) for path in sorted((ROOT / 'machines').glob('*/machine.json'))]


def held():
    """(kind, identity) of every bundled machine and every machine the catalog lists."""
    from .catalog import held_identities
    return [(m.kind, m.identity) for m in discovered()] + held_identities()


def free_synth_identity():
    """The lowest synth engine ID and page that the identity rules allow and neither a bundled
    machine nor the catalog holds."""
    rules = config()['identities']
    identities = held()
    engines = {identity.get('engine') for _kind, identity in identities}
    pages = {identity['page'] for kind, identity in identities if kind == 'synth'}
    engine = next(n for n in range(1, rules['engines'])
                  if n != rules['engine_none'] and n not in rules['stock_engines'] and n not in engines)
    page = next(p for p in range(rules['page_none'] + 1, rules['pages']) if p not in pages)
    return {'engine': engine, 'page': page}


def read_collection(path=COLLECTION, extra=()):
    """A collection file's entries, resolved: a bundled machine's ID, or the absolute
    folder of a machine elsewhere ({"path": ...}, relative to the file). Folders
    in `extra` follow them."""
    path = Path(path)
    value = json.loads(path.read_text(encoding='utf-8'))
    require(isinstance(value, dict) and set(value) == {'schema', 'machines'} and value['schema'] == 1 and
            isinstance(value['machines'], list), 'Unsupported collection file: ' + str(path))
    entries = []
    for entry in value['machines']:
        if isinstance(entry, dict):
            require(set(entry) == {'path'} and isinstance(entry['path'], str),
                    'Collection entries are IDs or {"path": ...}')
            entries.append({'path': str((path.parent / entry['path']).resolve())})
        else:
            entries.append(entry)
    return entries + [{'path': str(Path(folder).resolve())} for folder in extra]


def free_effect_identity():
    """The lowest effect ID that the identity rules allow and no bundled or catalog machine
    holds, on the FX page."""
    rules = config()['identities']
    used = {identity.get('effect') for _kind, identity in held()}
    free = [n for n in rules['effects'] if n not in used]
    require(free, 'Every effect ID is held: an effect needs one released by another machine')
    return {'effect': free[0], 'page': rules['effect_page']}


def collection(entries=None):
    """Machines in firmware order: the order sets startup, catalog and link layout.
    Entries come from read_collection; the default is profiles/collection.json."""
    entries = read_collection() if entries is None else entries
    require(isinstance(entries, list) and entries, 'Collection needs machines')
    machines = [machine_at(e['path']) if isinstance(e, dict) else machine(e) for e in entries]
    identities = [m.id for m in machines]
    require(len(set(identities)) == len(identities), 'Collection needs distinct machines')
    engines = [m.identity['engine'] for m in machines if 'engine' in m.identity]
    effects = [m.identity['effect'] for m in machines if 'effect' in m.identity]
    pages = [m.identity['page'] for m in machines if m.display != 'stock']
    require(len(set(engines)) == len(engines) and len(set(effects)) == len(effects) and len(set(pages)) == len(pages),
            'Conflicting engine, effect or page ID')
    check_analysis_budgets(machines)
    return machines


def check_analysis_budgets(machines):
    """Every machine's analysis budget comes from one stock-heap allowance."""
    total = sum(m.definition['resources'].get('sampleAnalysis', 0) for m in machines if m.kind == 'sample')
    require(total <= ANALYSIS_HEAP_LIMIT,
            'Sample analysis budgets exceed the collection limit of %d bytes' % ANALYSIS_HEAP_LIMIT)


def by_kind(machines, *kinds):
    return [m for m in machines if m.kind in kinds]
