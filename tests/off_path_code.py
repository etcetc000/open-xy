"""Off unless asked, by code: a build that selects no CPU capability links exactly what the same checkout links
with the CPU capabilities taken out of it.

`without_capabilities` copies this checkout and removes from the copy everything the CPU capabilities
(CPU_CAPABILITIES: the ones extensions/cpu names, and the deprecated idle) add: their registry entries (profiles/extensions.json), their firmware profile services and
edits (profiles/opxy-1.2.1.json), the SDK units of their services and the sources a unit capability chooses, the
template placeholders their code generation fills (@IDLE_*@), and the two places tooling/native.py names them
(UNIT_SOURCES, the load governor's profile check). Each removal must find what it removes, so a change to the
capabilities that this copy no longer matches fails here, naming the step, rather than passing unseen.

Each collection below is compiled and linked with `sdk.py compile` (pinned Blackfin tools) from both trees, with
profiling off and on, and the two records must be equal. A record holds:
- the whole linked module (module.bin): its size and digest, so any byte of the image that differs fails;
- for every function and data object the linked module keeps, its size and a digest of its bytes and
  relocations as its object file has them (unrelocated, so addresses do not enter), keyed by object file and
  symbol: when the module differs, these name where;
- per object file and allocated section, the bytes no function or data symbol covers (string literals,
  constant pools, jump tables, code under a bare label) with their relocations, keyed
  `object::[section NAME]`, so a difference there is named too;
- the size of each section of the linked module.
Nothing is stored: a change elsewhere in the SDK is in both trees and cannot fail this; a capability that leaks
into a build that did not ask for it (an edit, a unit, a generated line) can only be in one of them.

Run by hand (prints every difference, or nothing):
    python tests/off_path_code.py [--compiler <pinned Blackfin bundle>]
Public shapes no collection compiles (so this test does not see them): the keyboard-sampler base (the sample
fixture is a drum-page one) and profiling on with the sweep off. tests/test_off_unless_asked.py compares the
stock edits and SDK units of the same collections without the toolchain.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / 'fixtures/machines'

# What the copy without the capabilities leaves out of the checkout (its tests, docs and history are not built).
IGNORED = shutil.ignore_patterns('.git', 'node_modules', 'local', 'tests', 'docs', 'dist', '__pycache__')


# The CPU capabilities, in the order extensions/cpu names them; idle, deprecated, is no longer in it
# (docs/cpu-capabilities.md#idle) but its code is still the SDK's.
CPU_CAPABILITIES = ('boot_log_fix', 'idle', 'block_trims', 'output_slack', 'load_governor', 'pitch_constants',
                    'fast_helpers')


def cpu_capabilities(root):
    """The CPU capabilities the checkout at `root` has: those of CPU_CAPABILITIES in its registry
    (tests/test_off_path_code.py checks that they are what extensions/cpu names, and idle, once it exists)."""
    registry = json.loads((Path(root) / 'profiles/extensions.json').read_text(encoding='utf-8'))['capabilities']
    return [name for name in CPU_CAPABILITIES if name in registry]


def _replace(path, old, new, step):
    text = path.read_text(encoding='utf-8')
    if old not in text:
        raise AssertionError(f'without_capabilities, {step}: {old.strip()[:70]!r} is no longer in {path.name}; '
                             'update tests/off_path_code.py to the capabilities as they now are')
    path.write_text(text.replace(old, new), encoding='utf-8', newline='\n')


def without_capabilities(root, target):
    """A copy of the checkout at `root` in `target` with every CPU capability it has taken out (see this
    module's docstring). Returns `target`."""
    root, target = Path(root), Path(target)
    names = set(cpu_capabilities(root))
    shutil.copytree(root, target, ignore=IGNORED)
    registry_path = target / 'profiles/extensions.json'
    registry = json.loads(registry_path.read_text(encoding='utf-8'))
    for name in names:
        del registry['capabilities'][name]
    registry_path.write_text(json.dumps(registry, indent=2) + '\n', encoding='utf-8', newline='\n')
    profile_path = target / 'profiles/opxy-1.2.1.json'
    profile = json.loads(profile_path.read_text(encoding='utf-8'))
    profile['patches'] = [p for p in profile['patches'] if p.get('service') not in names]
    for name in names:
        del profile['services'][name]
    profile_path.write_text(json.dumps(profile, indent=2, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')
    # The copy's reviewed digest is its own profile's (tooling/pipeline.py reads it from builder/core.mjs).
    core = target / 'builder/core.mjs'
    text, count = re.subn(r"^export const PROFILE_SHA256 = '[0-9a-f]{64}';$",
                          "export const PROFILE_SHA256 = '%s';" % hashlib.sha256(profile_path.read_bytes()).hexdigest(),
                          core.read_text(encoding='utf-8'), flags=re.M)
    if count != 1:
        raise AssertionError('without_capabilities: builder/core.mjs has no PROFILE_SHA256')
    core.write_text(text, encoding='utf-8', newline='\n')
    sys.path.insert(0, str(root))
    try:
        from tooling import native
    finally:
        sys.path.pop(0)
    # The SDK units of their services, and the sources a unit capability chooses.
    unit_sources = getattr(native, 'UNIT_SOURCES', {})
    for unit, service in native.UNIT_SERVICES.items():
        if service in names:
            found = [f for f in (target / 'sdk/platform').rglob(unit + '.*') if f.suffix in ('.cpp', '.s')]
            if not found:
                raise AssertionError(f'without_capabilities: no source for the {service} unit {unit}')
            for f in found:
                f.unlink()
    for name, chosen in unit_sources.items():
        if name not in names:
            raise AssertionError(f'without_capabilities: the unit capability {name} is not the CPU extension\'s')
        for source in chosen.values():
            (target / source).unlink()
    # Code generation: every @IDLE_*@ placeholder of the SDK's templates goes, as generation without idle fills it.
    found = 0
    for template in (target / 'sdk/platform').rglob('*.in'):
        text = template.read_text(encoding='utf-8')
        bare = re.sub(r'@IDLE_[A-Z]+@', '', text)
        if bare != text:
            found += 1
            template.write_text(bare, encoding='utf-8', newline='\n')
    if 'idle' in names and not found:
        raise AssertionError('without_capabilities: no template has an @IDLE_*@ placeholder')
    native_py = target / 'tooling/native.py'
    if 'fast_helpers' in names:
        _replace(native_py, "UNIT_SOURCES = {'fast_helpers': {'divide': 'sdk/platform/blackfin/divide_fast.s',\n"
                            "                                 'memory': 'sdk/platform/blackfin/memory_fast.cpp'}}\n",
                 'UNIT_SOURCES = {}\n', 'UNIT_SOURCES')
    if 'load_governor' in names:
        _replace(native_py, "    load_governor.check_profile(profile, document(ROOT / extensions.PROFILE)['capabilities'])\n",
                 '', "the load governor's profile check")
    return target


KIT_ENGINE = '''#include "core.h"
namespace probekit {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}
inline void drum_defaults(opxy::u32 key, opxy::u8* model, opxy::u8* controls) {
    *model = static_cast<opxy::u8>(key & 1);
    for (unsigned i = 0; i < 8; ++i) controls[i] = 64;
}
inline void drum_select(Voice&, Shared&, opxy::u32, opxy::u32) {}
inline const char* model_name(opxy::u32 model) { return model ? "B" : "A"; }
inline const char* parameter_name(opxy::u32, opxy::u32) { return "P"; }
inline opxy::u32 parameter_count(opxy::u32) { return 8; }
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output, opxy::Parameters, opxy::Pitch pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        output[i] = static_cast<opxy::i32>(voice.phase >> 2) - 0x20000000;
        voice.phase += pitch[i];
    }
}
}
'''

# A public note player (docs/api.md, note players): four parameters, its own page.
PLAYER = {
    'machine.json': {'schema': 1, 'sdk': '1.8', 'kind': 'player', 'description': 'Test fixture: a player that adds a fifth.'},
    'definition.json': {'schema': 1, 'id': 'probeplayer', 'name': 'Probe Player', 'resources': {}, 'presets': {},
                        'parameters': [{'id': f'p{i}', 'label': f'P{i}', 'default': 0, 'display': 'percent',
                                        'mapping': {'kind': 'linear', 'minimum': 0, 'maximum': 1}} for i in range(4)]},
}
PLAYER_SOURCE = '''#include <opxy/player.hpp>
namespace probeplayer {
struct State { opxy::u32 held; };
bool press(State& state, const opxy::player::Controls&, opxy::u32 note, opxy::u32 velocity, opxy::player::Plan& plan) {
    ++state.held;
    plan.count = 2;
    plan.tones[0].pitch = static_cast<opxy::u8>(note);
    plan.tones[0].velocity = static_cast<opxy::u8>(velocity);
    plan.tones[0].delay = 0;
    plan.tones[1].pitch = static_cast<opxy::u8>(note + 7 > 127 ? note : note + 7);
    plan.tones[1].velocity = static_cast<opxy::u8>(velocity);
    plan.tones[1].delay = 60;
    return true;
}
bool release(State& state, opxy::u32) { if (state.held) --state.held; return false; }
void display(const State&, const opxy::player::Controls&, opxy::player::Display&) {}
}
'''
# A public settings extension that links code: a second Drumline page (docs/drumline-page.md), which turns on
# the drum, sample and Drumline services in a build.
PAGE = {'id': 'probepage', 'name': 'Probe page', 'description': 'Test fixture: a second Drumline page.',
        'claims': [], 'actions': [],
        'drumline_page': {'parameters': [{'label': 'FILTER', 'choices': ['L1', 'OPEN', 'H1'], 'default': 1},
                                         {'label': 'SRR', 'choices': ['OFF', 'S1']},
                                         {'label': 'FX I', 'role': 'send1'}, {'label': 'FX II', 'role': 'send2'}]}}
PAGE_SOURCE = '''#include <opxy/extension.hpp>
#include <opxy/drumline_page.hpp>

namespace opxy { namespace extensions { namespace probepage {
namespace {
void start(drumline::Voice&, const drumline::Choices&) {}
void process(drumline::Voice&, const drumline::Choices&, i32*) {}
}
extern constexpr Hooks hooks = Hooks();
extern const drumline::Processor drumline_processor = {start, process};
}}}
'''
# A public track-control synth: probesynth's voice with a control hook on aux track
# 12, a shared LFO and saved data. Links track_control (its table, hooks and project units), synth_pages and the
# aux-track units.
CONTROL_SOURCE = '''#include <opxy/track_control.hpp>
namespace probectl {
struct ControlState { opxy::u32 n; };
void control_reset(ControlState& s) { s.n = 0; }
void control(ControlState& s, const opxy::control::Input& in, opxy::control::Overlay& o) {
    ++s.n; opxy::control::add(o, 0, 1, in.values[0][1] + 1);
    if (in.transport.playing && !(s.n & 31)) opxy::control::play(o, 0, 60, 100, 100000);
}
opxy::u32 control_save(const ControlState& s, opxy::u8* d, opxy::u32 c) { if (c < 1) return 0; d[0] = static_cast<opxy::u8>(s.n); return 1; }
void control_load(ControlState& s, const opxy::u8* d, opxy::u32 b) { s.n = b ? d[0] : 0; }
}
'''
# A public master-processor settings extension with a one-node display. Links the master filter units (stock,
# hooks, panel).
MASTER_NS = 'namespace opxy { namespace extensions { namespace probemaster {'
MASTER = {
    'extension.json': {'id': 'probemaster', 'name': 'Probe master', 'description': 'Test fixture: a master processor.',
                       'claims': [], 'actions': [], 'master_processor': {'artwork': 'probemaster.svg'}},
    'definition.json': {'schema': 1, 'id': 'probemaster', 'name': 'Probe', 'presets': {},
                        'parameters': [{'id': 'sweep', 'label': 'filter', 'default': .5,
                                        'mapping': {'kind': 'linear', 'minimum': 0, 'maximum': 1}, 'display': 'percent'}],
                        'resources': {'display': {'stateBytes': 4, 'stateAlignment': 4, 'paths': 0, 'points': 0,
                                                  'nodes': 1, 'texts': 0, 'palette': 1}}},
}
MASTER_FILES = {
    'probemaster.svg': '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222">'
                       '<path id="probemaster_node_0" d="M 0 0 L 1 1" fill="none" stroke="#000000" stroke-width="1"/>'
                       '<path id="probemaster_palette_0" d="M 0 0 L 1 0 L 1 1 Z" fill="#000000" stroke="#000000" '
                       'stroke-width="1"/></svg>',
    'extension.cpp': '#include <opxy/extension.hpp>\n#include <opxy/master_processor.hpp>\n' + MASTER_NS +
                     '\nconst Hooks hooks={};void reset(){} void prepare(Parameters){} void process(i32* l,i32* r){'
                     'for(int i=0;i<64;++i){l[i]>>=1;r[i]>>=1;}}\n'
                     'const master::Processor processor={"probemaster",reset,prepare,process};\n}}}\n',
    'display.h': '#pragma once\n#include <opxy/draw.hpp>\n' + MASTER_NS +
                 '\nstruct DisplayState {u32 count;};using Frame=draw::Frame<0,0,1,0,1>;\n'
                 'void display_reset(DisplayState&);void display(DisplayState&,const draw::Input&,Frame&);}}}\n',
    'display.cpp': '#include "display.h"\n' + MASTER_NS +
                   '\nvoid display_reset(DisplayState& s){s.count=0;}\n'
                   'void display(DisplayState& s,const draw::Input&,Frame& f){++s.count;f={};} }}}\n',
}
EXTENSIONS = {'probepage', 'probemaster'}

# Public kit shapes (docs/api.md, drum kits): resources added to a plain two-model kit.
KITS = {
    'kit': {},
    'kit_padmix': {'padMix': 1, 'padControls': [{'label': 'FILTER', 'bits': 5, 'signed': True}]},
    'kit_choke': {'chokeSameKey': 1},
    'kit_groups': {'modelGroups': [{'label': 'ONE', 'count': 1}, {'label': 'TWO', 'count': 1}]},
}
# name -> machine folders; each is built with profiling off and on.
COLLECTIONS = {
    'effect': ['probefx'],
    'synth': ['probesynth'],
    'synth+effect': ['probesynth', 'probefx'],
    'all': ['probesynth', 'probefx', 'probesample', 'probeshift', 'probesources'],
    **{k: [k] for k in KITS},
    'player': ['probeplayer', 'probesynth'],
    'kit+page': ['kit', 'probepage'],
    'ctl': ['probectl', 'probefx'],
    'master': ['probesynth', 'probemaster'],
    'bus': ['probebus', 'probesynth'],
    # A synth whose voices live on the heap, and a kit with SDK 1.21's value editing.
    'heap': ['voiceheap'],
    'kit21': ['probekit21'],
}


def kit_folder(folder, extra):
    """A two-model kit made from the probesynth fixture with the given extra resources."""
    source = FIXTURES / 'probesynth'
    definition = json.loads((source / 'definition.json').read_text(encoding='utf-8'))
    definition.update(id='probekit', name='Probe Kit', presets={})
    for control, parameter in enumerate(copy.deepcopy(definition['parameters'])):
        parameter.update(id=parameter['id'] + '_alt', label='Alt ' + str(control), control=control)
        definition['parameters'].append(parameter)
    definition['resources'].update(modelCount=2, **extra)
    folder.mkdir(parents=True)
    (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
    (folder / 'machine.json').write_text(json.dumps({'schema': 1, 'sdk': '1.10', 'kind': 'drum',
                                                     'description': 'Test fixture: a two-model kit.'}), encoding='utf-8')
    (folder / 'core.h').write_text('#pragma once\n#include <opxy/audio.hpp>\nnamespace probekit {\n'
                                   'struct Voice { opxy::u32 phase; };\nstruct Shared {};\n}\n', encoding='utf-8')
    (folder / 'engine.cpp').write_text(KIT_ENGINE, encoding='utf-8')
    for name in ('display.cpp', 'display.h', 'panel.svg.fragment'):
        (folder / name).write_text((source / name).read_text(encoding='utf-8').replace('probesynth', 'probekit'),
                                   encoding='utf-8', newline='\n')


def player_folder(folder):
    """The public player fixture: a fifth above each note, a page of 24 input dots and a chord line."""
    folder.mkdir(parents=True)
    for name, data in PLAYER.items():
        (folder / name).write_text(json.dumps(data), encoding='utf-8')
    (folder / 'player.cpp').write_text(PLAYER_SOURCE, encoding='utf-8', newline='\n')
    nodes = [f'<circle id="probeplayer_dot_{i}" transform="matrix(1 0 0 1 {20 + 18 * i} 80)" r="4" fill="#000000" '
             'stroke="#000000" stroke-width="1"/>' for i in range(24)]
    nodes.append('<text id="probeplayer_text" transform="matrix(1 0 0 1 240 215)" fill="#FFFFFF" '
                 'font-family="UniversNextPro-Light" font-size="22">chords</text>')
    (folder / 'panel.svg.fragment').write_text('<g id="probeplayer_panel">\n' + '\n'.join(nodes) + '\n</g>\n',
                                               encoding='utf-8', newline='\n')


def page_folder(folder):
    """The public Drumline page extension fixture."""
    folder.mkdir(parents=True)
    (folder / 'extension.json').write_text(json.dumps(PAGE), encoding='utf-8')
    (folder / 'extension.cpp').write_text(PAGE_SOURCE, encoding='utf-8', newline='\n')


def control_folder(folder):
    """The public track-control synth fixture, made from probesynth."""
    shutil.copytree(FIXTURES / 'probesynth', folder)
    for f in folder.iterdir():
        f.write_text(f.read_text(encoding='utf-8').replace('probesynth', 'probectl'), encoding='utf-8', newline='\n')
    definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
    definition.update(id='probectl', name='Probe Ctl')
    definition['resources']['trackControl'] = {'stateBytes': 4, 'stateAlignment': 4, 'unique': True, 'aux': [12],
                                               'sharedLfo': True, 'savedBytes': 16}
    (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
    machine = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
    machine['sdk'] = '1.11'
    machine.pop('identity', None)
    (folder / 'machine.json').write_text(json.dumps(machine), encoding='utf-8')
    (folder / 'control.cpp').write_text(CONTROL_SOURCE, encoding='utf-8', newline='\n')


def master_folder(folder):
    """The public master-processor extension fixture."""
    folder.mkdir(parents=True)
    for name, data in MASTER.items():
        (folder / name).write_text(json.dumps(data), encoding='utf-8')
    for name, text in MASTER_FILES.items():
        (folder / name).write_text(text, encoding='utf-8', newline='\n')


def bus_folder(folder):
    """The effect fixture with a bus shift (resources.busShift, SDK 1.16): idle changes its generated adapter."""
    shutil.copytree(FIXTURES / 'probefx', folder)
    for f in folder.iterdir():
        f.write_text(f.read_text(encoding='utf-8').replace('probefx', 'probebus'), encoding='utf-8', newline='\n')
    definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
    definition.update(id='probebus', name='Probe Bus')
    definition['resources']['busShift'] = 9
    (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
    machine = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
    machine['sdk'] = '1.16'
    machine.pop('identity', None)
    (folder / 'machine.json').write_text(json.dumps(machine), encoding='utf-8')


def machine_folders(work):
    """Every machine and extension folder the collections name, under `work`."""
    folders = {}
    for name in sorted({m for ms in COLLECTIONS.values() for m in ms}):
        target = work / 'machines' / name
        if name in KITS:
            kit_folder(target, KITS[name])
        elif name == 'probeplayer':
            player_folder(target)
        elif name == 'probectl':
            control_folder(target)
        elif name == 'probebus':
            bus_folder(target)
        elif name == 'probemaster':
            master_folder(target)
        elif name in EXTENSIONS:
            page_folder(target)
        else:
            shutil.copytree(FIXTURES / name, target)
        folders[name] = target
    return folders


# ---- ELF32 little-endian, just what the record needs ----
SHT_SYMTAB, SHT_RELA, SHT_NOBITS, SHT_REL = 2, 4, 8, 9
SHF_WRITE, SHF_ALLOC, SHF_EXEC = 1, 2, 4


def _cstr(data, at):
    return data[at:data.index(b'\0', at)].decode()


def read_elf(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError(f'{path}: not a little-endian ELF32 file')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2e)
    keys = ('name', 'type', 'flags', 'addr', 'offset', 'size', 'link', 'info', 'align', 'entsize')
    sections = [dict(zip(keys, struct.unpack_from('<10I', data, shoff + i * shentsize))) for i in range(shnum)]
    for s in sections:
        s['name'] = _cstr(data, sections[shstrndx]['offset'] + s['name'])
    symbols = []
    for s in sections:
        if s['type'] != SHT_SYMTAB:
            continue
        strings = sections[s['link']]['offset']
        for at in range(s['offset'], s['offset'] + s['size'], 16):
            name, value, size, info, _other, shndx = struct.unpack_from('<IIIBBH', data, at)
            symbols.append({'name': _cstr(data, strings + name), 'value': value, 'size': size,
                            'kind': info & 15, 'shndx': shndx})
    return data, sections, symbols


def object_record(path, linked):
    """{symbol: [size, digest]} for each function or object of one object file that the module keeps, and
    {'[section NAME]': [bytes, digest]} for the bytes of each allocated section that no function or object
    symbol covers (with the relocations among them), in each section the module may keep: one with a kept
    symbol, or one with no symbol at all (string literals, constant pools)."""
    data, sections, symbols = read_elf(path)
    relocations = {}
    for s in sections:
        if s['type'] not in (SHT_RELA, SHT_REL):
            continue
        width = 12 if s['type'] == SHT_RELA else 8
        for at in range(s['offset'], s['offset'] + s['size'], width):
            offset, info = struct.unpack_from('<II', data, at)
            addend = struct.unpack_from('<i', data, at + 8)[0] if width == 12 else 0
            target = symbols[info >> 8]
            name = sections[target['shndx']]['name'] if target['kind'] == 3 and target['shndx'] < len(sections) \
                else target['name']
            relocations.setdefault(s['info'], []).append((offset, info & 0xff, name, addend))
    record = {}
    for symbol in symbols:
        if symbol['kind'] not in (1, 2) or symbol['name'] not in linked or symbol['shndx'] == 0:
            continue
        if symbol['shndx'] >= len(sections):  # COMMON or ABS
            record[symbol['name']] = [symbol['size'], 'common']
            continue
        section = sections[symbol['shndx']]
        start, end = symbol['value'], symbol['value'] + symbol['size']
        h = hashlib.sha256()
        h.update(bytes([section['flags'] & (SHF_WRITE | SHF_ALLOC | SHF_EXEC)]))
        if section['type'] != SHT_NOBITS:
            h.update(data[section['offset'] + start:section['offset'] + end])
        for offset, kind, name, addend in sorted(relocations.get(symbol['shndx'], [])):
            if start <= offset < end:
                h.update(f'|{offset - start}:{kind}:{name}:{addend}'.encode())
        record[symbol['name']] = [symbol['size'], h.hexdigest()[:16]]
    for index, section in enumerate(sections):
        if not section['flags'] & SHF_ALLOC or not section['size']:
            continue
        own = [s for s in symbols if s['shndx'] == index and s['kind'] in (1, 2)]
        if own and not any(s['name'] in linked for s in own):
            continue
        covered = bytearray(section['size'])
        for s in own:
            for i in range(s['value'], min(s['value'] + s['size'], len(covered))):
                covered[i] = 1
        loose = [i for i in range(section['size']) if not covered[i]]
        if not loose:
            continue
        h = hashlib.sha256()
        h.update(bytes([section['flags'] & (SHF_WRITE | SHF_ALLOC | SHF_EXEC)]))
        h.update(','.join(map(str, loose)).encode())
        if section['type'] != SHT_NOBITS:
            h.update(bytes(data[section['offset'] + i] for i in loose))
        for offset, kind, name, addend in sorted(relocations.get(index, [])):
            if offset >= len(covered) or not covered[offset]:
                h.update(f'|{offset}:{kind}:{name}:{addend}'.encode())
        record[f"[section {section['name']}]"] = [len(loose), h.hexdigest()[:16]]
    return record


def module_record(out):
    """The record of one `sdk.py compile` output folder."""
    _data, sections, symbols = read_elf(out / 'module.elf')
    linked = {s['name'] for s in symbols if s['kind'] in (1, 2)}
    image = (out / 'module.bin').read_bytes()
    record = {'module': [len(image), hashlib.sha256(image).hexdigest()[:16]],
              'sections': {s['name']: s['size'] for s in sections if s['flags'] & SHF_ALLOC and s['size']},
              'symbols': {}}
    for obj in sorted(out.rglob('*.o')):
        key = obj.relative_to(out).as_posix()
        for name, value in object_record(obj, linked).items():
            record['symbols'][f'{key}::{name}'] = value
    return record


def toolchain():
    home = os.environ.get('OPXY_TEST_COMPILER')
    if home and (Path(home) / 'bin').is_dir():
        return Path(home)
    return None


def compile_record(sdk, folders, names, profiling, out, compiler):
    command = [sys.executable, str(Path(sdk) / 'sdk.py'), 'compile', '--toolchain', str(compiler / 'bin'),
               '--compiler', str(compiler), '--out', str(out), '--overwrite', '--profiling', profiling]
    for name in names:
        command += ['--extension' if name in EXTENSIONS else '--machine', str(folders[name])]
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    for attempt in range(2):
        run = subprocess.run(command, cwd=sdk, capture_output=True, text=True, timeout=1800, **hidden)
        # On Windows a freshly written object is now and then briefly unreadable to the
        # build's own objdump ("No such file"); one more try settles it.
        if not run.returncode or 'No such file' not in run.stdout + run.stderr:
            break
    if run.returncode:
        raise RuntimeError(f'{out.name}: sdk.py compile failed\n{run.stdout[-2000:]}\n{run.stderr[-4000:]}')
    return module_record(out)


def records(sdk, compiler, work, jobs=4):
    """{collection:profiling -> record} for every collection, built by the SDK at `sdk`."""
    work = Path(work)
    folders = machine_folders(work)
    tasks = [(f'{name}:{mode}', names, mode) for name, names in COLLECTIONS.items() for mode in ('off', 'on')]
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {key: pool.submit(compile_record, sdk, folders, names, mode, work / 'out' / key.replace(':', '-')
                                    .replace('+', '_'), compiler) for key, names, mode in tasks}
        return {key: f.result() for key, f in futures.items()}


def differences(expected, actual):
    """(name, readable line) for every difference between two records of one collection; name is 'module'
    (the whole linked image), 'section NAME', 'object::symbol' or 'object::[section NAME]' (bytes no symbol
    covers)."""
    lines = []
    if expected['module'] != actual['module']:
        lines.append(('module', f"module.bin: {expected['module'][0]} bytes {expected['module'][1]} -> "
                                f"{actual['module'][0]} bytes {actual['module'][1]}"))
    for name in sorted(set(expected['sections']) | set(actual['sections'])):
        a, b = expected['sections'].get(name), actual['sections'].get(name)
        if a != b:
            lines.append((f'section {name}', f'section {name}: {a} -> {b} bytes'))
    for key in sorted(set(expected['symbols']) | set(actual['symbols'])):
        a, b = expected['symbols'].get(key), actual['symbols'].get(key)
        if a == b:
            continue
        if a is None:
            lines.append((key, f'+ {key} ({b[0]} bytes)'))
        elif b is None:
            lines.append((key, f'- {key} ({a[0]} bytes)'))
        elif a[0] != b[0]:
            lines.append((key, f'~ {key}: {a[0]} -> {b[0]} bytes'))
        else:
            lines.append((key, f'~ {key}: same size ({a[0]} bytes), different bytes or relocations'))
    return lines


def compare(root, compiler, work, jobs=4):
    """(collection:profiling, readable line) for every difference between the checkout at `root` and the same
    checkout without the capabilities, over every collection."""
    work = Path(work)
    bare = without_capabilities(root, work / 'without')
    asked = records(root, compiler, work / 'with', jobs)
    expected = records(bare, compiler, work / 'bare', jobs)
    if sorted(asked) != sorted(expected):
        raise AssertionError('the two trees built different collections')
    return [(key, line) for key in sorted(expected) for _name, line in differences(expected[key], asked[key])]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--compiler', type=Path, default=toolchain(), help='pinned Blackfin bundle root')
    args = parser.parse_args()
    if not args.compiler:
        parser.error('no compiler: give --compiler or set OPXY_TEST_COMPILER')
    with tempfile.TemporaryDirectory() as work:
        found = compare(HERE.parent, args.compiler.resolve(), work)
    for key, line in found:
        print(f'{key}: {line}')
    sys.exit(1 if found else 0)


if __name__ == '__main__':
    main()
