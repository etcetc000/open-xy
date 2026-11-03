"""Project companions: the `.oxy` file beside a `.xy` project that says which machine
each of its numbers meant.

Projects save a track's machine by engine, effect and page number, and those numbers
belong to the layout of the firmware that saved them. A companion records them by
machine ID, so firmware with another layout can open the project with the right
machines. Stock never reads it. The format is in docs/projects.md."""
import argparse
import base64
import binascii
import json
from pathlib import Path
import re
import struct
import zlib

from .formats import require
from .machines import check_layout_numbers, read_layout, reject_constant
from .pipeline import config
from .stock_tables import EFFECT_COUNT, ENGINE_COUNT

FORMAT = 'open-xy-project'
VERSION = 1
SUFFIX = '.oxy'
LIMIT = 64 * 1024
MACHINES = 'open-xy.machines'
MACHINES_VERSION = 1
# What track control machines keep with a project, base64 by machine ID, and
# the stock role's controls kept aside for each aux track pattern a machine took
# over, by track and pattern index.
CONTROL = 'open-xy.control'
CONTROL_VERSION = 1
# The cpu meter's indicator, the project's setting: written only while on.
CPU = 'open-xy.cpu'
CPU_VERSION = 1
# The track and pattern keys of open-xy.control's aux section: indices 0..15.
AUX_KEYS = {str(i) for i in range(16)}
# owner.name: the owner is a GitHub user or organisation, lowercased; `open-xy` is the SDK's.
SECTION_NAME = re.compile(r'[a-z0-9][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)+')
# The machine IDs a companion may list, the firmware reader's rule (project_companion.cpp
# machine_name): a lowercase letter, then lowercase letters, digits or _, at most 15.
MACHINE_ID = re.compile(r'[a-z][a-z0-9_]{0,14}')
# Distinct machines one section may list, as many as the firmware reader holds: more
# than one firmware numbers (test_stock_tables), so it reads every companion it writes.
MACHINE_LIMIT = 256
# Engine and effect numbers a pattern can hold registered, the relocated tables' counts
# (project_companion.cpp kNumbers and kEffectNumbers); others are never mapped.
NUMBERS, EFFECT_NUMBERS = ENGINE_COUNT, EFFECT_COUNT
# What a listed machine missing here, or a number two listed machines claim, becomes:
# Prism on an instrument track, no effect on a send.
FALLBACK = {'engine': 18, 'effect': 0}


def companion_path(project):
    project = Path(project)
    require(project.suffix == '.xy', f'{project} is not a .xy project')
    return project.with_suffix(SUFFIX)


def binding(data):
    """What ties a companion to the project bytes it describes."""
    return {'bytes': len(data), 'crc32': f'{zlib.crc32(data):08x}'}


def parse(raw):
    """A companion's document, checked. ValueError means a reader ignores the whole file
    and trusts the project's numbers, as stock does."""
    require(len(raw) <= LIMIT, f'a companion is at most {LIMIT} bytes')
    value = json.loads(raw.decode('utf-8'), parse_constant=reject_constant)
    require(isinstance(value, dict) and value.get('format') == FORMAT, f'not an {FORMAT} companion')
    require(type(value.get('version')) is int and 1 <= value['version'], 'invalid companion version')
    require(value['version'] <= VERSION, f"companion version {value['version']} is newer than {VERSION}")
    project = value.get('project')
    require(isinstance(project, dict) and type(project.get('bytes')) is int and project['bytes'] >= 0 and
            isinstance(project.get('crc32'), str) and re.fullmatch(r'[0-9a-f]{8}', project['crc32']),
            'invalid project binding')
    sections = value.get('sections')
    require(isinstance(sections, dict), 'sections must be an object')
    for name, section in sections.items():
        require(SECTION_NAME.fullmatch(name), f'invalid section name {name!r}: use owner.name')
        require(isinstance(section, dict) and type(section.get('version')) is int and section['version'] >= 1,
                f'{name}: a section is an object with a positive integer version')
    machines = sections.get(MACHINES)
    if machines and machines['version'] <= MACHINES_VERSION:
        require(isinstance(machines.get('profile'), str) and isinstance(machines.get('machines'), dict),
                f'{MACHINES}: needs a profile and machines')
        check_machines(machines['machines'])
    # open-xy.control's entries are read one by one, as the firmware reads them,
    # skipping a malformed one (kept_data, aux_kept): none voids the companion.
    return value


def check_machines(machines):
    """A machines section's numbers by machine ID: an engine and page, or an effect and
    page, each an integer."""
    require(len(machines) <= MACHINE_LIMIT, f'{MACHINES}: lists more than {MACHINE_LIMIT} machines')
    for name, identity in machines.items():
        require(MACHINE_ID.fullmatch(name), f'{MACHINES}: invalid machine ID {name!r}')
        require(isinstance(identity, dict) and set(identity) in ({'engine', 'page'}, {'effect', 'page'}) and
                all(type(n) is int for n in identity.values()), f'{name}: invalid numbers in {MACHINES}')
    return machines


def describes(document, data):
    return document['project'] == binding(data)


def saved_numbers(document, data, profile=None):
    """The saving firmware's numbers by machine ID, or None when the companion does not
    describe these project bytes (stock or another tool saved the project since), is for
    another firmware, or has no machines section this reader knows."""
    section = document['sections'].get(MACHINES)
    if not describes(document, data) or not section or section['version'] > MACHINES_VERSION:
        return None
    if section['profile'] != (profile or config()['id']):
        return None
    return section['machines']


def kept_data(document, data):
    """What each track control machine kept with these project bytes, by machine ID."""
    section = document['sections'].get(CONTROL)
    if not describes(document, data) or not section or section['version'] > CONTROL_VERSION:
        return {}
    machines = section.get('machines', {})
    result = {}
    for name, kept in (machines.items() if isinstance(machines, dict) else ()):
        try:
            result[name] = base64.b64decode(kept, validate=True) if isinstance(kept, str) else None
        except binascii.Error:
            continue
        if result[name] is None:
            del result[name]
    return result


def aux_kept(document, data):
    """The stock role's eight controls kept aside for each aux track pattern a machine
    held in these project bytes, by track and pattern index; the firmware gives them
    back on release, or after a load when it lacks the machine."""
    section = document['sections'].get(CONTROL)
    if not describes(document, data) or not section or section['version'] > CONTROL_VERSION:
        return {}
    # As the firmware reads it (track_control_project.cpp): an entry that is not a
    # track and pattern index with eight words of base64 is skipped, as are early
    # test builds' per-track entries ("machine", "controls").
    def controls(value):
        try:
            data = base64.b64decode(value, validate=True) if isinstance(value, str) else b''
        except binascii.Error:
            return None
        return list(struct.unpack('<8i', data)) if len(data) == 32 else None
    aux = section.get('aux', {})
    result = {}
    for track, patterns in (aux.items() if isinstance(aux, dict) else ()):
        if track not in AUX_KEYS or not isinstance(patterns, dict):
            continue
        held = {int(pattern): controls(value) for pattern, value in patterns.items() if pattern in AUX_KEYS}
        held = {pattern: value for pattern, value in held.items() if value is not None}
        if held:
            result[int(track)] = held
    return result


def cpu_indicator(document, data):
    """Whether the cpu meter's indicator is on for the project: only a companion that
    still describes it, with a version 1 open-xy.cpu section saying so."""
    section = document['sections'].get(CPU)
    return bool(describes(document, data) and section and section['version'] == CPU_VERSION and
                section.get('indicator') is True)


def translation(saved, layout):
    """Each saved (slot, number) and what it is in `layout`: the machine's own number
    there, or None when the machine is not in it, which a reader opens as FALLBACK.
    An engine or effect number two listed machines claim is None too: neither can be
    trusted. Numbers not listed, or outside the records (NUMBERS, EFFECT_NUMBERS), stay as they are.
    Pages, which projects do not save and sample engines share, map without that rule."""
    result, claims = {}, {}
    for name, identity in saved.items():
        here = layout.get(name)
        for slot, number in identity.items():
            # Effects share stock's FX page; a page means nothing without its engine.
            if slot == 'page' and 'effect' in identity:
                continue
            if slot != 'page' and not 0 <= number < (EFFECT_NUMBERS if slot == 'effect' else NUMBERS):
                continue
            claims[(slot, number)] = claims.get((slot, number), 0) + 1
            result[(slot, number)] = here.get(slot) if here and set(here) == set(identity) else None
    return {key: None if key[0] != 'page' and claims[key] > 1 else number for key, number in result.items()}


def companion(data, layout, previous=None, profile=None, kept=None, aux=None, indicator=False):
    """The companion for project bytes `data` saved with `layout`, what track control
    machines `kept` (bytes by machine ID), the role's eight controls kept for `aux`
    track patterns machines held (by track and pattern index), and whether the cpu
    meter's `indicator` is on, as the firmware writes it. Other sections are kept from
    `previous` only while it still describes the same bytes."""
    sections = {}
    if previous and describes(previous, data):
        sections = {name: section for name, section in previous['sections'].items() if name not in (MACHINES, CPU)}
    if indicator:
        sections[CPU] = {'version': CPU_VERSION, 'indicator': True}
    sections[MACHINES] = {'version': MACHINES_VERSION, 'profile': profile or config()['id'],
                          'machines': check_layout_numbers(layout, 'the layout')}
    if kept or aux:
        sections[CONTROL] = {'version': CONTROL_VERSION}
    if kept:
        sections[CONTROL]['machines'] = {name: base64.b64encode(value).decode() for name, value in kept.items()}
    if aux:
        sections[CONTROL]['aux'] = {str(track): {str(pattern): base64.b64encode(struct.pack('<8i', *controls)).decode()
                                                 for pattern, controls in patterns.items()}
                                    for track, patterns in aux.items()}
    return canonical({'format': FORMAT, 'version': VERSION, 'project': binding(data), 'sections': sections})


def canonical(document):
    """The one form writers emit, so equal companions are equal bytes and diff line by line."""
    raw = (json.dumps(document, indent=2, sort_keys=True) + '\n').encode('utf-8')
    require(len(raw) <= LIMIT, f'the companion would exceed {LIMIT} bytes')
    return raw


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py project', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    command = parser.add_subparsers(dest='command', required=True)
    writing = command.add_parser('write', help="write each project's companion from the layout that saved it")
    writing.add_argument('--layout', type=Path, required=True, help='layout.json of the firmware that saved the projects')
    checking = command.add_parser('check', help="report whether each project's companion applies to it")
    for sub in (writing, checking):
        sub.add_argument('projects', type=Path, nargs='+', help='.xy project files')
    args = parser.parse_args(argv)
    try:
        layout = read_layout(args.layout) if args.command == 'write' else None
        for project in args.projects:
            data, path = project.read_bytes(), companion_path(project)
            previous = None
            if path.exists():
                try:
                    previous = parse(path.read_bytes())
                except ValueError as error:
                    if args.command == 'check':
                        raise ValueError(f'{path}: {error}') from error
            if args.command == 'write':
                path.write_bytes(companion(data, layout, previous))
                print(f'wrote {path}')
            elif previous is None:
                print(f'{project}: no companion; its numbers are read as saved')
            else:
                saved = saved_numbers(previous, data)
                print(f'{project}: ' + (f"companion applies: {', '.join(sorted(saved))}" if saved is not None else
                                        'companion does not describe this project; its numbers are read as saved'))
    except (ValueError, OSError) as error:
        parser.exit(1, f'FAIL: {error}\n')
    return 0
