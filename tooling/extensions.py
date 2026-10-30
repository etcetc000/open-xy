"""Firmware extensions: folders of their own, like machines, against the SDK's contracts.

An extension folder holds extension.json (its ID, the site's text, and the claims
and actions it uses), extension.cpp and tests/. The SDK holds the registry of
claims and actions (PROFILE) and the runtime; a build's collection lists the
extensions it links, in callback order. No dynamic loading or arbitrary hooks.
"""
from dataclasses import dataclass
from pathlib import Path
import re
import base64
import hashlib
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

    master_filter: dict = None

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
    require(isinstance(data, dict) and keys <= set(data) <= keys | {'note', 'master_filter'}, f'{root}: extension.json fields are '
            'id, name, description, claims, actions and optional note and master_filter')
    name = data['id']
    require(isinstance(name, str) and NAME.fullmatch(name) and name not in RESERVED, f'{root}: invalid extension id')
    for key in TEXT_LIMITS:
        require(key not in data or (isinstance(data[key], str) and 0 < len(data[key]) <= TEXT_LIMITS[key]),
                f'{name}: {key} is 1-{TEXT_LIMITS[key]} characters')
    master_filter = data.get('master_filter')
    if master_filter is not None:
        require(isinstance(master_filter, dict) and {'artwork'} <= set(master_filter) <= {'artwork', 'state_id'} and
                isinstance(master_filter['artwork'], str), f'{name}: master_filter declares artwork')
        state_id = master_filter.get('state_id', name)
        require(isinstance(state_id, str) and NAME.fullmatch(state_id) and state_id != 'eq',
                f'{name}: master_filter state_id is an extension ID other than eq')
        artwork = Path(master_filter['artwork'])
        require(not artwork.is_absolute() and '..' not in artwork.parts and
                re.fullmatch(r'[a-z][a-z0-9_]*\.svg', artwork.name) and
                (root / artwork).resolve().is_relative_to(root), f'{name}: invalid master filter artwork path')
        validate_master_artwork(root / artwork)
    claims, actions = registry()
    for field, known in (('claims', claims), ('actions', actions)):
        values = data[field]
        require(isinstance(values, list) and len(set(values)) == len(values) and
                all(isinstance(v, str) and v in known for v in values),
                f'{name}: unknown or repeated {field[:-1]}; the SDK declares them in {PROFILE} and {RUNTIME}')
    for action in data['actions']:
        needed = actions[action].get('requires')
        require(needed is None or needed in data['claims'], f'{name}: {action} requires the {needed} claim')
    require(not data['actions'] or data['claims'], f'{name}: actions are granted through a claim it holds')
    for path in root.rglob('*'):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES and '.git' not in path.relative_to(root).parts:
            validate_source_boundary(path.read_text(encoding='utf-8'), f'{name}/{path.relative_to(root).as_posix()}')
    return Extension(name, root, tuple(data['claims']), tuple(data['actions']),
                     {k: data[k] for k in TEXT_LIMITS if k in data}, master_filter)


def check_collection(extensions):
    """A build's extensions: distinct, within the limit, and no claim held twice."""
    require(len(extensions) <= MAX_EXTENSIONS, f'At most {MAX_EXTENSIONS} extensions')
    ids, owners = set(), {}
    claims, _actions = registry()
    for e in extensions:
        require(e.id not in ids, 'Duplicate extension ID: ' + e.id)
        ids.add(e.id)
        for claim in e.claims:
            require(claim not in owners, f'{claims[claim]["label"]} has multiple owners: {owners.get(claim)}, {e.id}')
            owners[claim] = e.id
    require(sum(e.master_filter is not None for e in extensions) <= 1, 'Master filter has multiple owners')
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
    lines = ['#include "extension_runtime.hpp"']
    for e in extensions:
        symbol = 'sdk_extension_' + e.id + '_hooks'
        lines.append('extern "C" const opxy::extensions::Hooks* ' + symbol + '() __asm__("' + symbol + '");')
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
    if extension.master_filter is not None:
        symbol = 'sdk_extension_' + extension.id + '_filter'
        with target.open('a', encoding='utf-8') as out:
            out.write('extern "C" const opxy::master::Filter* ' + symbol + '() __asm__("' + symbol + '");\n'
                      'extern "C" const opxy::master::Filter* ' + symbol + '() { return '
                      '&opxy::extensions::' + extension.id + '::filter; }\n')
    return target


def metadata(extensions, origins):
    """The kit's extension entries: the site's text, claims and actions, and an origin
    (official, catalog or personal, as for machines) from `origins` by ID."""
    claims, _actions = registry()
    rows = [dict(id=e.id, origin=origins[e.id], claims=list(e.claims), actions=list(e.actions), **e.text,
                 claim_labels={c: claims[c]['label'] for c in e.claims}) for e in extensions]
    for e, row in zip(extensions, rows):
        if e.master_filter is not None:
            data = validate_master_artwork(e.root / e.master_filter['artwork'])
            row['master_filter'] = dict(asset=Path(e.master_filter['artwork']).name,
                artwork=base64.b64encode(data).decode('ascii'), sha256=hashlib.sha256(data).hexdigest())
    return rows


def validate_master_artwork(path):
    """Standalone, authored path-only SVG, never stock content or external references."""
    import xml.etree.ElementTree as ET
    data = path.read_bytes()
    require(0 < len(data) <= 65536, 'Master filter SVG exceeds 64 KiB')
    root = ET.fromstring(data)
    require(root.tag == '{http://www.w3.org/2000/svg}svg' and root.get('viewBox') == '0 0 480 222',
            'Master filter SVG needs the 480 x 222 viewport')
    seen = set()
    for node in root.iter():
        require(node is root or node.tag == '{http://www.w3.org/2000/svg}path', 'Master panel uses path leaves')
        require(not any(k.lower().startswith('on') or 'href' in k.lower() or k == 'style' or
                        'url(' in v.lower() for k,v in node.attrib.items()), 'External/style SVG references refused')
        name = node.get('id')
        require(not name or name not in seen, 'Duplicate master SVG ID')
        if name: seen.add(name)
    return data


def generate_master_collection(folder, entries):
    """Immutable, side-effect-free accessor, callable on audio without runtime initialization."""
    entries = [e for e in entries if e.master_filter is not None]
    lines = ['#include "master_filter.hpp"']
    for e in entries:
        symbol = 'sdk_extension_' + e.id + '_filter'
        lines += ['extern "C" const opxy::master::Filter* ' + symbol + '() __asm__("' + symbol + '");']
    lines += ['namespace opxy { namespace platform { namespace master_filter {',
              'const master::Filter* descriptor() {']
    for e in entries:
        lines += ['    if(const auto* f=sdk_extension_' + e.id + '_filter())',
                  '        if(f->label && f->reset && f->prepare && f->process) {',
                  '            u32 n=0;while(n<16 && f->label[n])++n;',
                  '            if(n && n<16)return f;',
                  '        }']
    lines += ['    return nullptr;', '}', 'const char* owner() {']
    for e in entries:
        lines += ['    if(descriptor())return "' + e.master_filter.get('state_id', e.id) + '";']
    lines += ['    return "";', '}', 'const char* asset() {']
    for e in entries:
        lines += ['    if(descriptor())return "' + Path(e.master_filter['artwork']).name + '";']
    lines += ['    return "";', '}', '}}}', '']
    target = folder / 'master_filter_collection.cpp'
    target.write_text('\n'.join(lines), encoding='utf-8')
    return target
