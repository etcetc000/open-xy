"""Firmware extensions: folders of their own, like machines, against the SDK's contracts.

An extension folder holds extension.json (its ID, the site's text, and the claims
and actions it uses), extension.cpp and tests/. The SDK holds the registry of
claims and actions (PROFILE) and the runtime; a build's collection lists the
extensions it links, in callback order. No dynamic loading or arbitrary hooks.
"""
from dataclasses import dataclass
from pathlib import Path
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
MAX_EXTENSIONS = 16
HEADER = 'sdk/include/opxy/extension.hpp'
RUNTIME = 'sdk/platform/extension_runtime.hpp'
SOURCE_SUFFIXES = ('.cpp', '.cc', '.h', '.hpp', '.inc')


def registry():
    """The claims and actions an extension may declare: SDK contracts. Claims are
    data (a label and the key it governs); actions are one bit each in RUNTIME."""
    data = document(ROOT / PROFILE)
    require(set(data) == {'schema', 'claims', 'actions'} and data['schema'] == 3, 'Invalid extension registry')
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
    return claims, actions


@dataclass(frozen=True)
class Extension:
    id: str
    root: Path
    claims: tuple
    actions: tuple
    text: dict  # name, description and optional note: the site's row and details
    arrangement: bool = False

    def sources(self):
        """Every file of the extension, by name relative to its folder."""
        return {p.relative_to(self.root).as_posix(): p for p in sorted(self.root.rglob('*'))
                if p.is_file() and '.git' not in p.relative_to(self.root).parts}


def extension_at(folder):
    """The extension in `folder`, checked against the SDK's registry and source rules."""
    root = Path(folder).resolve()
    require((root / 'extension.json').is_file() and (root / 'extension.cpp').is_file(),
            f'{root}: an extension folder holds extension.json and extension.cpp')
    data = document(root / 'extension.json')
    keys = {'id', 'claims', 'actions', 'name', 'description'}
    require(isinstance(data, dict) and keys <= set(data) <= keys | {'note', 'arrangement'}, f'{root}: extension.json fields are '
            'id, name, description, claims, actions, optional note and arrangement')
    require(type(data.get("arrangement", False)) is bool, "arrangement must be a boolean")
    name = data['id']
    require(isinstance(name, str) and NAME.fullmatch(name) and name not in RESERVED, f'{root}: invalid extension id')
    for key in TEXT_LIMITS:
        require(key not in data or (isinstance(data[key], str) and 0 < len(data[key]) <= TEXT_LIMITS[key]),
                f'{name}: {key} is 1-{TEXT_LIMITS[key]} characters')
    claims, actions = registry()
    for field, known in (('claims', claims), ('actions', actions)):
        values = data[field]
        require(isinstance(values, list) and len(set(values)) == len(values) and
                all(isinstance(v, str) and v in known for v in values),
                f'{name}: unknown or repeated {field[:-1]}; the SDK declares them in {PROFILE} and {RUNTIME}')
    require(data.get('arrangement', False) == ('arrangement_surface' in data['claims']),
            'An arrangement surface declares its arrangement_surface claim')
    for action in data['actions']:
        needed = actions[action].get('requires')
        require(needed is None or needed in data['claims'], f'{name}: {action} requires the {needed} claim')
    require(not data['actions'] or data['claims'], f'{name}: actions are granted through a claim it holds')
    for path in root.rglob('*'):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES and '.git' not in path.relative_to(root).parts:
            validate_source_boundary(path.read_text(encoding='utf-8'), f'{name}/{path.relative_to(root).as_posix()}')
    return Extension(name, root, tuple(data['claims']), tuple(data['actions']),
                     {k: data[k] for k in TEXT_LIMITS if k in data}, data.get('arrangement', False))


def check_collection(extensions):
    """A build's extensions: distinct, within the limit, and no claim held twice."""
    require(len(extensions) <= MAX_EXTENSIONS, f'At most {MAX_EXTENSIONS} extensions')
    require(sum(e.arrangement for e in extensions) <= 1, "Arrangement surface has multiple owners")
    ids, owners = set(), {}
    claims, _actions = registry()
    for e in extensions:
        require(e.id not in ids, 'Duplicate extension ID: ' + e.id)
        ids.add(e.id)
        for claim in e.claims:
            require(claim not in owners, f'{claims[claim]["label"]} has multiple owners: {owners.get(claim)}, {e.id}')
            owners[claim] = e.id
    return extensions


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


def generate_collection(folder, extensions):
    """The collection and the registry tables the runtime reads: each claim's
    key, and the claims that grant each action (0: any claim its holder owns)."""
    claims, actions = registry()
    claim_bit = {name: 1 << i for i, name in enumerate(claims)}
    action_bit = {name: 1 << i for i, name in enumerate(actions)}
    mask = lambda table, names: '0x%xu' % sum(table[n] for n in names)
    table = lambda values: ', '.join(values) or '0'
    lines = ['#include "extension_runtime.hpp"', '#include "arrangement.hpp"', '#include "arrangement_descriptor.hpp"']
    for e in extensions:
        symbol = 'sdk_extension_' + e.id + '_hooks'
        lines.append('extern "C" const opxy::extensions::Hooks* ' + symbol + '() __asm__("' + symbol + '");')
        if e.arrangement:
            symbol = 'sdk_extension_' + e.id + '_surface'
            lines.append('extern "C" const opxy::platform::ArrangementDescriptor* ' + symbol + '() __asm__("' + symbol + '");')
    lines += ['namespace opxy { namespace platform {', 'namespace {',
              'ExtensionEntry collection_entries[' + str(max(1, len(extensions))) + '];',
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
    for e in extensions:
        lines += ['    if (const auto* hooks = sdk_extension_' + e.id + '_hooks())',
                  '        collection_entries[count++] = {"%s", %s, %s, hooks};' % (
                      e.id, mask(claim_bit, e.claims), mask(action_bit, e.actions))]
    lines += ['    return collection_entries;', '}', '}}', '']
    lines += ['extern "C" const opxy::extensions::ArrangementSurface* sdk_arrangement_owner() {']
    for e in extensions:
        if e.arrangement:
            lines += ['    const auto* hooks = sdk_extension_' + e.id + '_hooks();',
                      '    return hooks ? hooks->arrangement : nullptr;']
    if not any(e.arrangement for e in extensions):
        lines += ['    return nullptr;']
    lines += ['}', '']
    lines += ['extern "C" const opxy::platform::ArrangementDescriptor* sdk_arrangement_descriptor() {']
    for e in extensions:
        if e.arrangement:
            lines += ['    return sdk_extension_' + e.id + '_surface();']
    if not any(e.arrangement for e in extensions):
        lines += ['    return nullptr;']
    lines += ['}', '']
    target = folder / 'extension_collection.cpp'
    target.write_text('\n'.join(lines), encoding='utf-8')
    return target


def generate_extension(folder, extension):
    """Stable C accessor; an omitted browser object resolves it to kit_absent."""
    target = folder / ('extension_' + extension.id + '.cpp')
    source = (extension.root / 'extension.cpp').as_posix()
    symbol = 'sdk_extension_' + extension.id + '_hooks'
    target.write_text('#include "' + source + '"\n'
                      'extern "C" const opxy::extensions::Hooks* ' + symbol + '() __asm__("' + symbol + '");\n'
                      'extern "C" const opxy::extensions::Hooks* ' + symbol + '() {\n'
                      '    return &opxy::extensions::' + extension.id + '::hooks;\n}\n', encoding='utf-8')
    if extension.arrangement:
        from . import arrangement
        arrangement.header(folder)
        surface = 'sdk_extension_' + extension.id + '_surface'
        with target.open('a', encoding='utf-8') as file:
            file.write('#include "' + (ROOT / 'sdk/platform/opxy-1.2.1/arrangement.cpp').as_posix() + '"\n' +
                       'extern "C" const opxy::platform::ArrangementDescriptor* ' + surface +
                       '() __asm__("' + surface + '");\n' +
                       'extern "C" const opxy::platform::ArrangementDescriptor* ' + surface +
                       '() { return &arrangement_descriptor; }\n')
    return target


def metadata(extensions, origins):
    """The kit's extension entries: the site's text, claims and actions, and an origin
    (official, catalog or personal, as for machines) from `origins` by ID."""
    claims, _actions = registry()
    return [dict(id=e.id, origin=origins[e.id], arrangement=e.arrangement, claims=list(e.claims), actions=list(e.actions), **e.text,
                 claim_labels={c: claims[c]['label'] for c in e.claims}) for e in extensions]


def checked_gui_trampoline(profile, module, symbols, text):
    """Qualify the only undecodable BF+ instructions in the GUI poll trampoline."""
    symbol = symbols.get('sdk_stock_extension_gui_poll')
    if symbol is None:
        return None
    patch = next((p for p in profile['patches'] if p.get('symbol') == 'sdk_extension_gui_poll'), None)
    before = bytes.fromhex('00e8030000de4d8d68b40000')
    expected = before + bytes.fromhex('00dc0b8c48720000')
    require(patch and patch['address'] == '0x8c0b723c' and patch['length'] == 12 and
            patch['before'] == before.hex() and patch.get('encoding') == 'absolute_jump' and
            patch.get('call') is False, 'Unexpected GUI poll hook contract')
    start, size = symbol['value'], symbol['size']
    offset = start - profile['module_base']
    require(size == len(expected) and start % 2 == 0 and
            text['address'] <= start and start + size <= text['address'] + text['size'] and
            0 <= offset and offset + size <= len(module) and module[offset:offset + size] == expected,
            'Unexpected GUI poll trampoline')
    return start, start + size
