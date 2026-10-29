"""Curated, ordered firmware extensions. No dynamic loading or arbitrary hooks."""
import re
from .contributors import validate_source_boundary
from .formats import require
from .pipeline import ROOT, document

PROFILE = 'profiles/extensions.json'


NAME = re.compile(r'[a-z][a-z0-9_]{0,31}')
# IDs name an object beside the SDK's own (sdk/extension_<id>.o) and a C++ namespace.
RESERVED = {'runtime', 'collection', 'stock', *'''alignas alignof and and_eq asm auto bitand bitor bool break
    case catch char char16_t char32_t class compl const constexpr const_cast continue decltype default delete
    do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long
    mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public register
    reinterpret_cast return short signed sizeof static static_assert static_cast struct switch template this
    thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t
    while xor xor_eq'''.split()}
# The site's row and details: one line saying what it does and how, and an
# optional note for its one limit. Short on purpose: people scan, not read.
TEXT_LIMITS = {'name': 32, 'description': 140, 'note': 100}
# Open-XY's own extensions are official (shown as Core); contributed ones are community.
ORIGINS = ('official', 'community')
HEADER = 'sdk/include/opxy/extension.hpp'
RUNTIME = 'sdk/platform/extension_runtime.hpp'


def _document():
    data = document(ROOT / PROFILE)
    require(set(data) == {'schema', 'claims', 'actions', 'extensions'} and data['schema'] == 2,
            'Invalid extension collection schema')
    claims, actions = data['claims'], data['actions']
    require(isinstance(claims, dict) and isinstance(actions, dict), 'Claims and actions are registries')
    require(len(claims) <= 32 and len(actions) <= 32, 'At most 32 claims and 32 actions')
    for name, claim in claims.items():
        require(NAME.fullmatch(name) and isinstance(claim, dict) and set(claim) == {'label', 'key'} and
                isinstance(claim['label'], str) and 0 < len(claim['label']) <= 40 and
                type(claim['key']) is int and 0 <= claim['key'] < 256, 'Invalid claim: ' + name)
    for name, action in actions.items():
        require(NAME.fullmatch(name) and isinstance(action, dict) and set(action) <= {'requires'} and
                ('requires' not in action or action['requires'] in claims), 'Invalid action: ' + name)
    return data


def registry():
    """The claims and actions an extension may declare: SDK contracts, one bit each in RUNTIME."""
    data = _document()
    return data['claims'], data['actions']


def collection():
    data = _document()
    claims, actions = data['claims'], data['actions']
    entries = data['extensions']
    require(isinstance(entries, list) and len(entries) <= 16, 'At most 16 extensions')
    ids, owners = set(), {}
    for entry in entries:
        require(isinstance(entry, dict) and set(entry) == {'id', 'origin', 'claims', 'actions'} and
                entry['origin'] in ORIGINS, 'Invalid extension declaration')
        name = entry['id']
        require(isinstance(name, str) and NAME.fullmatch(name) and name not in RESERVED, 'Invalid extension ID')
        require(name not in ids, 'Duplicate extension ID: ' + name)
        ids.add(name)
        for field, known in (('claims', claims), ('actions', actions)):
            values = entry[field]
            require(isinstance(values, list) and len(set(values)) == len(values) and
                    all(isinstance(v, str) and v in known for v in values),
                    f'Unknown or repeated {field[:-1]} in {name}: declare it in {PROFILE} and {RUNTIME}')
        for claim in entry['claims']:
            require(claim not in owners, f'{claims[claim]["label"]} has multiple owners: {owners.get(claim)}, {name}')
            owners[claim] = name
        for action in entry['actions']:
            needed = actions[action].get('requires')
            require(needed is None or needed in entry['claims'], f'{action} requires the {needed} claim')
        root = ROOT / 'extensions' / name
        require((root / 'extension.cpp').is_file(), 'Missing extension source: ' + name)
        for path in root.rglob('*'):
            if path.is_file() and path.suffix in ('.cpp', '.cc', '.h', '.hpp', '.inc'):
                validate_source_boundary(path.read_text(encoding='utf-8'), path.relative_to(ROOT).as_posix())
    return entries


def events():
    """The public events: the Hooks callbacks in HEADER, in order."""
    text = (ROOT / HEADER).read_text(encoding='utf-8')
    body = re.search(r'struct Hooks \{(.*?)\n\};', text, re.S)
    require(body, 'Missing Hooks in ' + HEADER)
    return re.findall(r'constexpr Hooks with_(\w+)\(', body.group(1))


def check_profile(profile):
    """Every extension hook names the events it feeds, and every event has a site."""
    known, fed = set(events()), set()
    for patch in profile['patches']:
        if patch.get('service') == 'extensions':
            listed = patch.get('events')
            require(patch['kind'] == 'hook' and isinstance(listed, list) and listed and
                    len(set(listed)) == len(listed) and set(listed) <= known,
                    'An extension hook names the events it feeds: ' + str(patch.get('symbol')))
            fed |= set(listed)
        else:
            require('events' not in patch, 'Only extension hooks name events')
    require(fed == known, 'Events without a firmware site: ' + ', '.join(sorted(known - fed)))


def generate_collection(folder):
    """The collection and the registry tables the runtime reads: each claim's
    key, and the claims that grant each action (0: any claim its holder owns)."""
    entries = collection()
    claims, actions = registry()
    claim_bit = {name: 1 << i for i, name in enumerate(claims)}
    action_bit = {name: 1 << i for i, name in enumerate(actions)}
    mask = lambda table, names: '0x%xu' % sum(table[n] for n in names)
    table = lambda values: ', '.join(values) or '0'
    lines = ['#include "extension_runtime.hpp"']
    for entry in entries:
        symbol = 'sdk_extension_' + entry['id'] + '_hooks'
        lines.append('extern "C" const opxy::extensions::Hooks* ' + symbol + '() __asm__("' + symbol + '");')
    lines += ['namespace opxy { namespace platform {', 'namespace {',
              'ExtensionEntry collection_entries[' + str(max(1, len(entries))) + '];',
              '// ' + ', '.join(claims) if claims else '// no claims',
              'const u32 claim_keys[] = {' + table([str(c['key']) for c in claims.values()]) + '};',
              '// ' + ', '.join(actions) if actions else '// no actions',
              'const u32 action_claims[] = {' + table(
                  [mask(claim_bit, [a['requires']] if 'requires' in a else []) for a in actions.values()]) + '};',
              'const ExtensionRegistry registry_tables = {claim_keys, %d, action_claims, %d};' % (
                  len(claims), len(actions)),
              '}',
              'const ExtensionRegistry& extension_registry() { return registry_tables; }',
              'const ExtensionEntry* extension_collection(u32& count) {', '    count = 0;']
    for entry in entries:
        lines += ['    if (const auto* hooks = sdk_extension_' + entry['id'] + '_hooks())',
                  '        collection_entries[count++] = {"%s", %s, %s, hooks};' % (
                      entry['id'], mask(claim_bit, entry['claims']), mask(action_bit, entry['actions']))]
    lines += ['    return collection_entries;', '}', '}}', '']
    target = folder / 'extension_collection.cpp'
    target.write_text('\n'.join(lines), encoding='utf-8')
    return target


def generate_extension(folder, name):
    """Stable C accessor; an omitted browser object resolves it to kit_absent."""
    require(name in {entry['id'] for entry in collection()}, 'Unknown extension')
    target = folder / ('extension_' + name + '.cpp')
    source = (ROOT / 'extensions' / name / 'extension.cpp').as_posix()
    symbol = 'sdk_extension_' + name + '_hooks'
    target.write_text('#include "' + source + '"\n'
                      'extern "C" const opxy::extensions::Hooks* ' + symbol + '() __asm__("' + symbol + '");\n'
                      'extern "C" const opxy::extensions::Hooks* ' + symbol + '() {\n'
                      '    return &opxy::extensions::' + name + '::hooks;\n}\n', encoding='utf-8')
    return target


def metadata():
    result = []
    for entry in collection():
        data = document(ROOT / 'extensions' / entry['id'] / 'extension.json')
        require({'name', 'description'} <= set(data) <= set(TEXT_LIMITS) and
                all(isinstance(value, str) and 0 < len(value) <= TEXT_LIMITS[key] for key, value in data.items()),
                'Invalid extension display metadata: ' + entry['id'])
        claims, _actions = registry()
        result.append(dict(entry, **data, claim_labels={c: claims[c]['label'] for c in entry['claims']}))
    return result
