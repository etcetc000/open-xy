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
# A page claim's page, by name, as the runtime's page numbers (extension_runtime.hpp):
# its owner's layer opens while that stock page shows on an instrument track.
PAGES = {'lfo': 1}
# The most nodes one layer may name.
LAYER_NODES = 96


def registry():
    """The claims and actions an extension may declare: SDK contracts. Claims are
    data (a label and the key it governs, or the page whose layer it opens); actions
    are one bit each in RUNTIME."""
    data = document(ROOT / PROFILE)
    require(set(data) == {'schema', 'claims', 'actions'} and data['schema'] == 3, 'Invalid extension registry')
    claims, actions = data['claims'], data['actions']
    require(isinstance(claims, dict) and isinstance(actions, dict), 'Claims and actions are registries')
    require(len(claims) <= 32 and len(actions) <= 32, 'At most 32 claims and 32 actions')
    for name, claim in claims.items():
        require(NAME.fullmatch(name) and isinstance(claim, dict) and set(claim) in ({'label', 'key'}, {'label', 'page'}) and
                isinstance(claim['label'], str) and 0 < len(claim['label']) <= 40 and
                (type(claim.get('key')) is int and 0 <= claim['key'] < 256 or claim.get('page') in PAGES),
                'Invalid claim: ' + name)
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

    master_processor: dict = None
    # Its layer's node names (extension.json "layer"), drawn from layer.svg.fragment.
    layer: tuple = ()

    def layer_ids(self):
        return tuple(f'{self.id}_{name}' for name in self.layer)

    @property
    def master_filter(self):
        return self.master_processor  # Legacy Python caller compatibility.

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
    require(isinstance(data, dict) and keys <= set(data) <= keys | {'note', 'master_filter', 'master_processor', 'layer'},
            f'{root}: extension.json fields are id, name, description, claims, actions and optional note, '
            'master_processor and layer')
    name = data['id']
    require(isinstance(name, str) and NAME.fullmatch(name) and name not in RESERVED, f'{root}: invalid extension id')
    for key in TEXT_LIMITS:
        require(key not in data or (isinstance(data[key], str) and 0 < len(data[key]) <= TEXT_LIMITS[key]),
                f'{name}: {key} is 1-{TEXT_LIMITS[key]} characters')
    require(not ('master_filter' in data and 'master_processor' in data), 'Declare master_processor only once')
    master_filter = data.get('master_processor', data.get('master_filter'))
    if master_filter is not None:
        require(isinstance(master_filter, dict) and {'artwork'} <= set(master_filter) <= {'artwork', 'state_id'} and
                isinstance(master_filter['artwork'], str), f'{name}: master_processor declares artwork')
        state_id = master_filter.get('state_id', name)
        require(isinstance(state_id, str) and NAME.fullmatch(state_id) and state_id != 'eq',
                f'{name}: master_processor state_id is an extension ID other than eq')
        artwork = Path(master_filter['artwork'])
        require(not artwork.is_absolute() and '..' not in artwork.parts and
                re.fullmatch(r'[a-z][a-z0-9_]*\.svg', artwork.name) and
                (root / artwork).resolve().is_relative_to(root), f'{name}: invalid master processor artwork path')
        from .definitions import validate_definition
        definition=validate_definition(document(root/'definition.json'),'master')
        require(definition['id']==name, 'Master definition ID differs from extension ID')
        require((root/'display.h').is_file() and (root/'display.cpp').is_file(), 'Master processor needs shared display.h/display.cpp')
        validate_master_artwork(root / artwork, definition)
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
    layer = data.get('layer')
    if layer is not None:
        require(isinstance(layer, dict) and set(layer) == {'nodes'} and isinstance(layer['nodes'], list) and
                0 < len(layer['nodes']) <= LAYER_NODES and len(set(layer['nodes'])) == len(layer['nodes']) and
                all(isinstance(n, str) and NAME.fullmatch(n) and n != 'layer' for n in layer['nodes']),
                f'{name}: layer is {{"nodes": [...]}}: 1 to {LAYER_NODES} distinct names, none "layer"')
        require(any('page' in claims[c] for c in data['claims']), f'{name}: a layer needs a page claim')
    extension = Extension(name, root, tuple(data['claims']), tuple(data['actions']),
                          {k: data[k] for k in TEXT_LIMITS if k in data}, master_filter,
                          tuple(layer['nodes']) if layer else ())
    if extension.layer:
        layer_artwork(extension)
    return extension


def page_claims(extension):
    """The page claims an extension holds."""
    claims, _actions = registry()
    return [c for c in extension.claims if 'page' in claims[c]]


def layer_artwork(extension):
    """An extension's layer artwork (layer.svg.fragment): one <g id="<id>_layer">
    in the authored SVG subset holding exactly its declared nodes, each a path
    or text the overlay can show, hide and move."""
    from .native import panel_root, read
    from .svg_safety import validate_nodes
    path = extension.root / 'layer.svg.fragment'
    require(path.is_file(), f'{extension.id}: a layer needs layer.svg.fragment')
    fragment = read(path)
    root = panel_root(fragment, f'{extension.id}/layer.svg.fragment')
    nodes = list(root.iter())
    require(root.tag == 'g' and root.get('id') == extension.id + '_layer',
            f'layer.svg.fragment must be <g id="{extension.id}_layer">')
    validate_nodes(nodes)
    ids = [node.get('id') for node in nodes[1:] if node.get('id')]
    require(len(ids) == len(set(ids)) and set(ids) == set(extension.layer_ids()),
            f'{extension.id}: layer.svg.fragment must hold exactly the layer nodes: ' + ', '.join(extension.layer_ids()))
    # The overlay moves path and text leaves (extension_overlay.cpp.in, bind).
    require(all(node.tag in ('path', 'text') for node in nodes[1:] if node.get('id')),
            f'{extension.id}: each layer node is a path or text')
    return fragment


def layer_ranges(extensions):
    """Each extension's first node and count in the SDK's overlay, by ID."""
    # Every ID in the overlay is unique: one extension's node may not read as another's.
    ids = [i for e in extensions if e.layer for i in (e.id + '_layer', *e.layer_ids())]
    require(len(ids) == len(set(ids)), 'Two extensions\' layers share an ID: ' +
            ', '.join(sorted({i for i in ids if ids.count(i) > 1})))
    ranges, first = {}, 0
    for e in extensions:
        ranges[e.id] = (first, len(e.layer))
        first += len(e.layer)
    return ranges


def overlay_artwork(template, extensions):
    """The overlay's artwork: the SDK's empty template, then each extension's layer."""
    require(template.count(b'</svg>') == 1, 'Unexpected overlay artwork boundary')
    return template.replace(b'</svg>', b''.join(layer_artwork(e) for e in extensions if e.layer) + b'</svg>')


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
    processors = [e for e in extensions if e.master_processor is not None]
    ids = [e.master_processor.get('state_id', e.id) for e in processors]
    require(len(ids) == len(set(ids)), 'Master processor state IDs must be unique')
    assets = [Path(e.master_processor['artwork']).name for e in processors]
    require(len(assets) == len(set(assets)), 'Master processor assets must be unique')
    return extensions


# A layer's painting comes from the SDK's overlay screen (extension_overlay.cpp.in),
# which startup registers: a screen, not a patched call.
SCREEN_EVENTS = {'draw'}
# Services whose hooks feed extension events.
EVENT_SERVICES = {'extensions', 'page_layers'}


def events():
    """The public events: the Hooks callbacks in HEADER, in order."""
    text = (ROOT / HEADER).read_text(encoding='utf-8')
    body = re.search(r'struct Hooks \{(.*?)\n\};', text, re.S)
    require(body, 'Missing Hooks in ' + HEADER)
    return re.findall(r'constexpr Hooks with_(\w+)\(', body.group(1))


def check_profile(profile):
    """Every extension hook names the events it feeds, and every event has a site."""
    known, fed = set(events()), set(SCREEN_EVENTS)
    for patch in profile['patches']:
        if patch.get('service') in EVENT_SERVICES:
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
    key and page (0: none), and the claims that grant each action (0: any claim
    its holder owns)."""
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
              'const u32 claim_keys[] = {' + table([str(c.get('key', 0)) for c in claims.values()]) + '};',
              'const u32 claim_pages[] = {' + table([str(PAGES.get(c.get('page'), 0)) for c in claims.values()]) + '};',
              '// ' + ', '.join(actions) if actions else '// no actions',
              'const u32 action_claims[] = {' + table(
                  [mask(claim_bit, [a['requires']] if 'requires' in a else []) for a in actions.values()]) + '};',
              'const ExtensionRegistry registry_tables = {claim_keys, %d, action_claims, %d, claim_pages};' % (
                  len(claims), len(actions)),
              '}',
              'const ExtensionRegistry& extension_registry() { return registry_tables; }',
              'const ExtensionEntry* extension_collection(u32& count) {', '    count = 0;']
    ranges = layer_ranges(extensions)
    for e in extensions:
        lines += ['    if (const auto* hooks = sdk_extension_' + e.id + '_hooks())',
                  '        collection_entries[count++] = {"%s", %s, %s, hooks, %d, %d};' % (
                      e.id, mask(claim_bit, e.claims), mask(action_bit, e.actions), *ranges[e.id])]
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
    if extension.master_processor is not None:
        legacy = 'master_filter' in document(extension.root/'extension.json')
        symbol = 'sdk_extension_' + extension.id + '_processor'
        with target.open('a', encoding='utf-8') as out:
            out.write('#include <opxy/master_processor.hpp>\n')
            out.write('extern "C" const opxy::master::Processor* ' + symbol + '() __asm__("' + symbol + '");\n'
                      'extern "C" const opxy::master::Processor* ' + symbol + '() { return '
                      '&opxy::extensions::' + extension.id + ('::filter' if legacy else '::processor') + '; }\n')
            source, _ = generate_processor_panel(folder / ('panel_' + extension.id), extension)
            out.write('#include "' + source.as_posix() + '"\n')
    return target


def metadata(extensions, origins):
    """The kit's extension entries: the site's text, claims and actions, and an origin
    (official, catalog or personal, as for machines) from `origins` by ID."""
    claims, _actions = registry()
    rows = [dict(id=e.id, origin=origins[e.id], claims=list(e.claims), actions=list(e.actions), **e.text,
                 claim_labels={c: claims[c]['label'] for c in e.claims}) for e in extensions]
    for e, row in zip(extensions, rows):
        if page_claims(e):
            row['page_layer'] = True
        if e.master_processor is not None:
            data = validate_master_artwork(e.root / e.master_processor['artwork'])
            row['master_processor'] = dict(state_id=e.master_processor.get('state_id',e.id), asset=Path(e.master_processor['artwork']).name,
                artwork=base64.b64encode(data).decode('ascii'), sha256=hashlib.sha256(data).hexdigest())
    return rows


def validate_master_artwork(path, definition=None):
    """Standalone, authored path-only SVG, never stock content or external references."""
    import xml.etree.ElementTree as ET
    data = path.read_bytes()
    require(0 < len(data) <= 65536, 'Master processor SVG exceeds 64 KiB')
    from .svg_safety import validate_xml
    validate_xml(data)
    root = ET.fromstring(data)
    require(root.tag == '{http://www.w3.org/2000/svg}svg' and root.get('viewBox') == '0 0 480 222',
            'Master processor SVG needs the 480 x 222 viewport')
    from .svg_safety import validate_nodes
    for node in root.iter():node.tag=node.tag.removeprefix('{http://www.w3.org/2000/svg}')
    leaves=[n for n in root.iter() if n is not root]
    root.tag='g'
    validate_nodes(list(root.iter()))
    if definition is not None:
        from .native import check_bindings, check_palette
        by_id = {n.get('id'):n for n in leaves if n.get('id')}
        check_palette(by_id, check_bindings(by_id,definition['id'],definition['resources']['display']))
    return data


def generate_master_collection(folder, entries):
    """Immutable registration accessors: omitted extension objects return nullptr."""
    entries = [e for e in entries if e.master_processor is not None]
    lines = ['#include "master_processor.hpp"']
    for e in entries:
        symbol='sdk_extension_'+e.id
        lines += ['extern "C" const opxy::master::Processor* '+symbol+'_processor() __asm__("'+symbol+'_processor");',
                  'extern "C" const opxy::platform::master_processor::Display* '+symbol+'_panel() __asm__("'+symbol+'_panel");']
    lines += ['namespace opxy { namespace platform { namespace master_processor {',
              'Registration registration(u32 type) {', '    if(!type)return {};', '    u32 selected=0;']
    from .definitions import control_positions
    for e in entries:
        definition=document(e.root/'definition.json');positions=control_positions(definition)
        rows=[]
        for i,p in enumerate(definition['parameters']):
            steps=len(p['mapping']['labels']) if p['mapping']['kind']=='enum' else 0
            rows.append('{"%s","%s",%d,%d,%d}' % (p['id'],p['label'],round(p['default']*0x7fffffff),positions[i],steps))
        symbol='sdk_extension_'+e.id
        lines += ['    if(const auto* p='+symbol+'_processor()) {',
                  '        if(p->label && p->reset && p->prepare && p->process) {',
                  '            u32 n=0;while(n<16 && p->label[n])++n;',
                  '            if(n && n<16 && ++selected==type) {',
                  '                static const Parameter parameters[] = {'+','.join(rows)+'};',
                  '                return {"'+e.master_processor.get('state_id',e.id)+'",p,parameters,'+str(len(rows))+','+symbol+'_panel()};',
                  '            }', '        }', '    }']
    lines += ['    return {};','}', 'u32 processor_count() {', '    u32 n=0;',
              '    while(n<max_processors && registration(n+1).processor)++n;', '    return n;', '}', '}}}', '']
    target=folder/'master_filter_collection.cpp';target.write_text('\n'.join(lines),encoding='utf-8');return target


def generate_processor_panel(folder, extension):
    """One bounded GUI instance per included processor, using the shared display binding."""
    from .adapters import generate_display
    from .native import display_ids, header_ids
    folder=Path(folder);folder.mkdir(exist_ok=True)
    e=extension;definition=document(e.root/'definition.json')
    generate_display(folder,definition=definition,kind='master',namespace='opxy::extensions::'+e.id,
                     symbol_prefix='sdk_master_'+e.id, binding_namespace='master_binding_'+e.id)
    ids=display_ids(e.id,definition['resources']['display'])
    source='#include "display_binding.cpp"\n'
    source+='#define OPXY_MASTER_PANEL_ID '+e.id+'\n#define OPXY_MASTER_BINDING master_binding_'+e.id+'\n'
    source+='static const char* const master_display_ids[] = {'+','.join('"'+i+'"' for i in ids)+'};\n'
    source+='static const char* const master_header_ids[] = {'+','.join('"'+i+'"' for i in header_ids(e.id))+'};\n'
    source+='#define OPXY_MASTER_ASSET "'+Path(e.master_processor['artwork']).name+'"\n'
    source+='#include "'+(ROOT/'sdk/platform/opxy-1.2.1/master_processor_panel.hpp').as_posix()+'"\n'
    symbol='sdk_extension_'+e.id+'_panel'
    source+='extern "C" const opxy::platform::master_processor::Display* '+symbol+'() __asm__("'+symbol+'");\n'
    source+='extern "C" const opxy::platform::master_processor::Display* '+symbol+'() {\n'
    source+='    using namespace opxy::platform::master_processor::panels::'+e.id+';\n'
    source+='    static const opxy::platform::master_processor::Display display={frame,reset_project,parameter_reset};return &display;\n}\n'
    target=folder/'panel.cpp';target.write_text(source,encoding='utf-8');return target,e.root
