"""Machine discovery: each machines/<id>/machine.json declares what the SDK builds.

A machine's folder name is its stock name: the engine type the device lists,
its catalog and preset entries, its screen file and its symbol prefix.
machine.json declares the kind, stock IDs and build inputs. Saved presets and Open-XY layouts refer to
the name and IDs, so they never change once published; the SDK checks them against the
stock tables and the collection.
"""
from dataclasses import dataclass
import json
import re

from .definitions import ANALYSIS_HEAP_LIMIT, ROOT, stock_name, validate_definition
from .formats import require
from .pipeline import config

KINDS = ('synth', 'sample', 'effect')
FIELDS = {'schema', 'kind', 'description', 'identity', 'sources', 'includes', 'icon', 'presetDefaults', 'dryParameter'}
# Builder card glyph: SVG path data in a 46 x 48 box, drawn as a stroke.
ICON = re.compile(r'[MLHVCSQTAZmlhvcsqtaz][MLHVCSQTAZmlhvcsqtaz0-9 .,-]{0,511}')
COLLECTION = ROOT / 'profiles/collection.json'


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
    def root(self):
        return ROOT / 'machines' / self.id


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


def machine(identity):
    stock_name(identity)
    root = ROOT / 'machines' / identity
    path = root / 'machine.json'
    require(path.is_file() and path.resolve() == path.absolute(), 'Unknown or redirected machine: ' + identity)
    value = json.loads(path.read_text(encoding='utf-8'))
    require(isinstance(value, dict) and value.get('schema') == 1 and
            set(value) <= FIELDS,
            'Unexpected machine.json fields: ' + identity)
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
                   icon=icon, dry_parameter=dry_parameter)


def discovered():
    """Every machine folder in the repository, in name order."""
    return [machine(path.parent.name) for path in sorted((ROOT / 'machines').glob('*/machine.json'))]


def free_synth_identity():
    """The lowest synth engine ID and page that the identity rules allow and no machine here uses."""
    rules = config()['identities']
    machines = discovered()
    engines = {m.identity.get('engine') for m in machines}
    pages = {m.identity['page'] for m in machines}
    engine = next(n for n in range(1, rules['engines'])
                  if n != rules['engine_none'] and n not in rules['stock_engines'] and n not in engines)
    page = next(p for p in range(rules['page_none'] + 1, rules['pages']) if p not in pages)
    return {'engine': engine, 'page': page}


def collection():
    """Machines in firmware order: the order sets startup, catalog and link layout."""
    value = json.loads(COLLECTION.read_text(encoding='utf-8'))
    identities = value['machines']
    require(isinstance(identities, list) and identities and len(set(identities)) == len(identities),
            'Collection needs distinct machines')
    machines = [machine(identity) for identity in identities]
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
