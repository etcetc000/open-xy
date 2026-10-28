"""Machine discovery: each machine folder's machine.json declares what the SDK builds.

A machine lives in machines/<id>/ or in a folder of its own anywhere, such as
its own repository. Its definition's id is its stock name: the engine type the
device lists, its catalog and preset entries, its screen file and its symbol
prefix. machine.json declares the kind, the SDK version it targets, stock IDs
and build inputs. Saved presets and Open-XY layouts refer to the name and IDs,
so they never change once published; the SDK checks them against the stock
tables and the collection.
"""
from dataclasses import dataclass, replace
import json
import os
from pathlib import Path
import re

from .definitions import ANALYSIS_HEAP_LIMIT, ROOT, SDK_1_2_TEXTS, own_screen, paged, stock_name, validate_definition
from .formats import require
from .pipeline import config

KINDS = ('synth', 'sample', 'effect')
FIELDS = {'schema', 'sdk', 'kind', 'description', 'identity', 'sources', 'includes', 'icon', 'presetDefaults',
          'presets', 'dryParameter'}
# The public machine contract: headers, definition schema, artwork rules and
# callbacks. A machine names the version it was written for; the SDK builds a
# machine of the same major version and an equal or older minor version.
# 1.1 adds display note activity; 1.2 an effect's delay buffer (resources.bufferBytes),
# its tempo (resources.transport) and division captions; 1.3 the machine's own presets,
# a press that switches a synth's whole page and up to 16 display text slots.
# 1.4 adds the reusable header and partial second pages sharing unassigned controls.
# 1.5 adds a sample engine's own drawing page and single-sample assignment.
SDK_VERSION = (1, 5)
# Builder card glyph: SVG path data in a 46 x 48 box, drawn as a stroke.
ICON = re.compile(r'[MLHVCSQTAZmlhvcsqtaz][MLHVCSQTAZmlhvcsqtaz0-9 .,-]{0,511}')
# A machine's own presets (machine.json `presets`): <name>.json files the builder
# installs beside its init preset; default.json replaces the generated one.
# builder/core.mjs checks them again.
PRESET_NAME = re.compile(r'[a-z0-9][a-z0-9_-]{0,23}')
PRESET_COUNT = 16
PRESET_BYTES = 16 * 1024
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
    presets: dict = None  # name -> preset document, from machine.json `presets`
    icon: str = ''
    dry_parameter: int | None = None
    requested: dict = None  # the numbers machine.json asks for, when a layout gave others

    # The stock name and the symbol prefix are the machine's name.
    @property
    def native_name(self):
        return self.id

    @property
    def prefix(self):
        return self.id

    def source(self, relative=''):
        """A machine file as the build names it: its absolute path."""
        return self.root.as_posix() + ('/' + relative if relative else '')

    def files(self):
        """Every authored file, keyed as the build's provenance names it."""
        return {'external/' + self.id + '/' + p.relative_to(self.root).as_posix(): p
                for p in tree(self.root) if p.suffix != '.md'}

    def contains(self, path):
        """Whether a path lies inside this machine's folder, where build output must never go."""
        return Path(os.path.abspath(path)).is_relative_to(self.root)


def relative_path(value):
    """A path inside a folder, written with forward slashes: no '..' and no root."""
    return (isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9_./-]+', value) is not None and
            '..' not in value.split('/') and not value.startswith('/'))


def _relative(machine_root, value):
    require(relative_path(value), 'Invalid machine path: ' + str(value))
    path = (machine_root / value).resolve()
    require(path.is_relative_to(machine_root.resolve()) and path.exists(), 'Missing machine path: ' + value)
    return value


def check_preset(value, identity):
    """A preset as the device saves it (version 4), for this machine and pointing at no samples."""
    engine = value.get('engine') if isinstance(value, dict) else None
    params = engine.get('params') if isinstance(engine, dict) else None
    require(isinstance(value, dict) and value.get('version') == 4 and value.get('platform') == 'OP-XY' and
            value.get('type') == identity and value.get('regions') == [] and isinstance(params, list) and
            len(params) == 8 and all(type(n) is int and 0 <= n <= 32767 for n in params),
            f'A preset is a version 4 OP-XY preset of {identity}, with eight engine params and no sample regions')
    return value


def reject_constant(name):
    raise ValueError(f'{name} is not JSON')


def read_presets(root, folder, identity):
    """Each <name>.json in the machine's presets folder, by name."""
    path = root / _relative(root, folder)
    require(path.is_dir(), 'presets names a folder: ' + folder)
    # Notes and hidden files, such as a Finder .DS_Store, are not presets.
    files = sorted(p for p in path.iterdir() if p.suffix != '.md' and not p.name.startswith('.'))
    require(0 < len(files) <= PRESET_COUNT, f'A presets folder holds 1 to {PRESET_COUNT} presets')
    presets = {}
    for file in files:
        require(file.is_file() and file.suffix == '.json' and PRESET_NAME.fullmatch(file.stem),
                f'Invalid preset file {file.name}: <name>.json, the name lowercase letters, digits, - and _, '
                'at most 24')
        data = file.read_bytes()
        require(len(data) <= PRESET_BYTES, f'{file.name}: a preset is at most {PRESET_BYTES} bytes')
        try:
            value = json.loads(data.decode('utf-8'), parse_constant=reject_constant)
        except ValueError as error:
            raise ValueError(f'{file.name}: invalid JSON: {error}') from error
        presets[file.stem] = check_preset(value, identity)
    return presets


def check_identity(rules, kind, value, screen=False):
    """IDs a machine may claim under the profile's identity rules (the browser builder applies the same rules).

    Engines take a free entry of the relocated table, never the stock "none" value; a
    synth draws its own page above the stock "none" page, a sample engine an ID only
    the relocated table holds and a stock sample page (or a relocated page with screen=True),
    an effect a free FX ID on the FX page.
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
        require(rules['engine_none'] < number, 'Sample engines need a relocated-table engine ID')
        require(rules['page_none'] < page < rules['pages'] if screen else page in rules['sample_pages'],
                'Sample engines need a relocated page with a display, otherwise a stock sample page')
    else:
        require(rules['page_none'] < page < rules['pages'], 'A machine with its own screen needs a relocated page')
    return {slot: number, 'page': page}


def sdk_version(text):
    """A machine's declared SDK version, checked against this SDK."""
    require(text is not None, 'machine.json needs "sdk": "%d.%d". A machine from before SDK 1.0: '
            'see docs/display.md#migrating-from-before-sdk-10' % SDK_VERSION)
    require(isinstance(text, str) and re.fullmatch(r'\d+\.\d+', text), 'machine.json needs "sdk": "<major>.<minor>"')
    major, minor = map(int, text.split('.'))
    current = '%d.%d' % SDK_VERSION
    require(major == SDK_VERSION[0] and minor <= SDK_VERSION[1],
            f'The machine targets SDK {text}; this SDK is {current} and builds {SDK_VERSION[0]}.0 to {current}')
    return major, minor


def require_sdk(version, minimum, feature):
    """A feature newer than the SDK version the machine declares is refused, not misread."""
    require(version >= minimum, f'{feature} needs "sdk": "%d.%d" in machine.json' % minimum)


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
    version = sdk_version(value.get('sdk'))
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
    if 'bufferBytes' in definition['resources']:
        require_sdk(version, (1, 2), 'An effect buffer (bufferBytes)')
    if definition['resources'].get('transport'):
        require_sdk(version, (1, 2), "An effect's tempo (transport)")
    if any(p['display'] == 'division' for p in definition['parameters']):
        require_sdk(version, (1, 2), 'A division control')
    if paged(definition):
        require_sdk(version, (1, 3), 'A press that switches the page ("press": "page")')
        if len(definition['parameters']) < 8:
            require_sdk(version, (1, 4), 'A partially filled second page')
    if kind == 'synth' and definition['resources']['display']['texts'] > SDK_1_2_TEXTS:
        require_sdk(version, (1, 3), 'More than %d display text slots' % SDK_1_2_TEXTS)
    require(definition['id'] == identity, 'Machine directory and definition identity disagree')
    dry_parameter = None
    if 'dryParameter' in value:
        require(kind == 'effect' and isinstance(value['dryParameter'], str),
                'dryParameter names a normalized control on an effect')
        matches = [i for i, p in enumerate(definition['parameters']) if p['id'] == value['dryParameter']]
        require(len(matches) == 1, 'dryParameter must name a declared control')
        dry_parameter = matches[0]
        require(definition['parameters'][dry_parameter]['mapping'] ==
                dict(kind='linear', minimum=0, maximum=1), 'dryParameter must map linearly from 0 to 1')
    # Drawing is independent of the machine's audio kind.
    screen = own_screen(definition, kind)
    display = 'drawing' if screen else 'stock'
    numbers = check_identity(config()['identities'], kind, value.get('identity'), screen)
    if kind == 'sample' and (screen or 'singleSample' in definition['resources']):
        require_sdk(version, (1, 5), "A sample engine's own page (resources.display) or singleSample")
        require(not definition['resources'].get('singleSample', 0) or numbers['page'] != 7,
                'singleSample uses stock page 2 or a drawing display, not drum page 7')
    # The native plan compiles extra translation units only into synths.
    if kind != 'synth':
        require(not sources and not includes, 'Sample engines and effects compile one fx.cpp or sample.cpp')
    # Pad settings live in the stock drum page's pad records, whose fade the SDK then owns.
    if kind == 'sample' and definition['resources'].get('padSettings', 0):
        require(numbers['page'] == 7, 'Pad settings and drum controls need stock page 7')
    defaults = value.get('presetDefaults', {})
    require(isinstance(defaults, dict) and set(defaults) <= {'amp'}, 'Invalid preset defaults')
    amp = defaults.get('amp', {})
    require(isinstance(amp, dict) and set(amp) <= {'attack', 'decay', 'release', 'sustain'} and
            all(type(v) is int and 0 <= v <= 32767 for v in amp.values()), 'Invalid preset amp defaults')
    presets = None
    if 'presets' in value:
        require_sdk(version, (1, 3), "A machine's own presets")
        require(kind != 'effect' and isinstance(value['presets'], str), 'presets names a folder; effects have none')
        presets = read_presets(root, value['presets'], identity)
    return Machine(id=identity, kind=kind, name=definition['name'], description=value['description'],
                   display=display, sources=sources, includes=includes,
                   definition=definition,
                   identity=numbers, preset_defaults=defaults,
                   root=root, icon=icon, dry_parameter=dry_parameter, presets=presets)


# Machine numbers. Projects store a track's engine, effect and page numbers, so a
# machine keeps its numbers in a firmware for good. The first Open-XY firmware gave
# its own machines fixed numbers, held in every firmware whether or not it holds the
# machine (FIXED_NUMBERS). Every other machine is given numbers by a layout: the firmware's record of
# every number it has given out, including to machines since dropped, which stay
# held so an old project never opens another machine. A machine new to a layout
# gets the numbers it asks for when they are free, else the lowest free ones.
# builder/core.mjs assignNumbers applies the same rules (tests/fixtures/numbers.json).
LAYOUT_FORMAT = 'opxy-layout/1'
FIXED_NUMBERS = {'fm2op': {'engine': 34, 'page': 82}, 'macroosc': {'engine': 26, 'page': 83},
                 'ringmod': {'effect': 1, 'page': 21}, 'timestretch': {'engine': 35, 'page': 7}}


def number_pools(rules):
    """The numbers a machine new to a layout may be given, lowest first. Engines come only
    from the relocated table above stock's "none" value: the gaps in stock's own table
    below it are left for stock."""
    engines = [n for n in range(rules['engine_none'] + 1, rules['engines']) if n not in rules['stock_engines']]
    return dict(engine=engines, page=list(range(rules['page_none'] + 1, rules['pages'])), effect=list(rules['effects']))


def occupied(rules, identity):
    """What an identity holds: its engine or effect number, and its own page. Effects share
    the FX page and sample engines a stock sample page. Stock reads a send track's effect
    number as its engine, so an effect also holds that engine number: no engine shares it."""
    keys = [(slot, identity[slot]) for slot in ('engine', 'effect') if slot in identity]
    if 'effect' in identity:
        keys.append(('engine', identity['effect']))
    if 'effect' not in identity and identity['page'] not in rules['sample_pages']:
        keys.append(('page', identity['page']))
    return keys


def read_layout(path):
    """A layout file's numbers by machine ID. The builder's saved selection is a layout too."""
    value = json.loads(Path(path).read_text(encoding='utf-8'))
    profile = config()
    require(isinstance(value, dict) and value.get('format') == LAYOUT_FORMAT and isinstance(value.get('machines'), dict),
            f'{path} is not a machine layout ({LAYOUT_FORMAT})')
    # Earlier profiles with the same identity rules number machines the same way.
    require(value.get('profile') in (profile['id'], *profile.get('layout_profiles', ())),
            f"{path} is a layout for {value.get('profile')}, not {profile['id']}")
    return check_layout_numbers(value['machines'], 'the layout')


def check_layout_numbers(machines, where):
    """Layout numbers by machine ID: an engine and page, or an effect and page, each."""
    for name, identity in machines.items():
        stock_name(name)
        require(isinstance(identity, dict) and set(identity) in ({'engine', 'page'}, {'effect', 'page'}) and
                all(type(n) is int for n in identity.values()), f'{name}: invalid numbers in {where}')
    return machines


def layout_document(numbers):
    return {'format': LAYOUT_FORMAT, 'profile': config()['id'], 'machines': dict(sorted(numbers.items()))}


def number_layout(rules, fixed, layout, machines):
    """The layout after numbering `machines`: every number `layout` held, the fixed
    machines' and any given out now. Machines are (id, kind, requested identity[, drawing]), fixed
    ones (id, kind, identity); a stock-page sample engine keeps that page."""
    pools = number_pools(rules)
    numbers, holders = {}, {}

    def hold(name, identity):
        for key in occupied(rules, identity):
            require(holders.setdefault(key, name) == name,
                    f'{name} and {holders[key]} both hold {key[0]} {key[1]} in this layout')
        numbers[name] = dict(identity)

    for name, _kind, identity in fixed:
        hold(name, identity)
    for name, identity in (layout or {}).items():
        if name in numbers:
            require(numbers[name] == identity, f'{name} has fixed numbers; the layout gives it others')
        elif 'effect' in identity and identity['effect'] not in pools['effect']:
            # An effect number later found unusable on stock (its controls never
            # moved) is let go: the machine is numbered again like a new one.
            continue
        else:
            hold(name, identity)
    for entry in machines:
        name, kind, requested = entry[:3]
        screen = entry[3] if len(entry) == 4 else False
        if name in numbers:
            check_identity(rules, kind, numbers[name], screen)
            require(kind != 'sample' or screen or numbers[name]['page'] == requested['page'],
                    f'{name} now asks for stock page {requested["page"]}; the layout holds another')
            continue
        identity = dict(requested)
        for slot in ('effect',) if kind == 'effect' else ('engine', 'page') if kind == 'synth' or screen else ('engine',):
            choices = ([requested[slot]] if requested[slot] in pools[slot] else []) + pools[slot]
            # An effect number is also held as an engine number (occupied).
            keys = lambda n: [(slot, n)] + ([('engine', n)] if slot == 'effect' else [])
            identity[slot] = next((n for n in choices if not any(k in holders for k in keys(n))), None)
            require(identity[slot] is not None, f'No free {slot} number for {name}: every one is held in this layout')
        hold(name, check_identity(rules, kind, identity, screen))
    return numbers


def assign_numbers(machines, layout=None, fixed=None):
    """The machines with their numbers in this layout, and the layout after. `fixed` maps
    IDs to fixed numbers, by default FIXED_NUMBERS; a machine with fixed numbers asks for them."""
    fixed = FIXED_NUMBERS if fixed is None else fixed
    for m in machines:
        require(fixed.get(m.id, m.identity) == m.identity, f'{m.id} has fixed numbers; its machine.json asks for others')
    numbers = number_layout(config()['identities'], [(name, None, identity) for name, identity in fixed.items()],
                            layout, [(m.id, m.kind, m.identity, m.display != 'stock') for m in machines])
    assigned = [m if numbers[m.id] == m.identity else replace(m, identity=numbers[m.id], requested=m.identity)
                for m in machines]
    return assigned, numbers


def free_synth_identity():
    """Numbers to ask for in a new synth's machine.json: the lowest that no fixed numbers hold."""
    rules = config()['identities']
    held = {key for identity in FIXED_NUMBERS.values() for key in occupied(rules, identity)}
    pools = number_pools(rules)
    return {slot: next(n for n in pools[slot] if (slot, n) not in held) for slot in ('engine', 'page')}


def read_collection(path=None, extra=()):
    """A collection file's entries, resolved: each machine's absolute folder
    ({"path": ...}, relative to the file). Folders in `extra` follow them."""
    if path is None:
        return [{'path': str(Path(folder).resolve())} for folder in extra]
    path = Path(path)
    value = json.loads(path.read_text(encoding='utf-8'))
    require(isinstance(value, dict) and set(value) == {'schema', 'machines'} and value['schema'] == 1 and
            isinstance(value['machines'], list), 'Unsupported collection file: ' + str(path))
    entries = []
    for entry in value['machines']:
        require(isinstance(entry, dict) and set(entry) == {'path'} and isinstance(entry['path'], str),
                'Collection entries are {"path": ...}: the SDK holds no machines of its own')
        entries.append({'path': str((path.parent / entry['path']).resolve())})
    return entries + [{'path': str(Path(folder).resolve())} for folder in extra]


def free_effect_identity():
    """Numbers to ask for in a new effect's machine.json: the lowest effect number not fixed."""
    rules = config()['identities']
    used = {identity.get('effect') for identity in FIXED_NUMBERS.values()}
    free = [n for n in rules['effects'] if n not in used]
    require(free, 'Every effect number is fixed')
    return {'effect': free[0], 'page': rules['effect_page']}


def collection(entries=None, layout=None):
    """Machines in firmware order, with their numbers in the layout: the order sets
    startup, catalog and link layout, and which machine new to the layout is numbered first.
    Entries come from read_collection."""
    entries = [] if entries is None else entries
    require(isinstance(entries, list) and entries,
            'Collection needs machines: pass --machine <folder>, or --collection local/catalog/collection.json '
            'after python sdk.py catalog fetch')
    machines = [machine_at(e['path']) for e in entries]
    identities = [m.id for m in machines]
    require(len(set(identities)) == len(identities), 'Collection needs distinct machines')
    machines, _numbers = assign_numbers(machines, layout)
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
