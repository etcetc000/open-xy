"""Build, package and verify a collection of machines for stock OP-XY 1.2.1. No device operations.

Every machine in the collection is compiled and linked into one module
inside the 2 MiB reservation with relocated engine and screen tables. Object
names are part of the published browser kit (its linker orders objects by name),
so their naming scheme is fixed.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from . import benchmark, screen, startup_label, stock_tables
from .adapters import (catalog_entry, generate_display, generate_fx_object, generate_registration,
                       generate_registration_stock, generate_sample_voice,
                       generate_sample_callbacks, generate_screen, generate_screen_order,
                       generate_synth_controls, generate_project_companion_table, generate_voice)
from .contributors import validate_source_boundary
from .firmware import active_services, assemble, hook_instruction, hook_target, patch_active
from .formats import elf32, elf_sections, require, sha
from .machines import FIXED_NUMBERS, by_kind, collection, layout_document, read_collection, read_layout, tree
from .pipeline import (PROFILE, ROOT, check_patch_boundaries, child, config, document, execute, fresh_output, host_pins, host_platform,
                       read, require_release_host, resolve_toolchain, save_json, tool_paths)
from .startup import generate as generate_startup

LINKER = 'linker/opxy-1.2.1.ld'
TARGET = 'opxy-1.2.1'
# The stock instrument catalog every build extends, kept in the repository for
# development: the builder distributes it too. Firmware always comes from the device.
STOCK_CONTENT = ROOT / 'profiles/opxy-1.2.1/content'
DEFINITIONS = dict(stock_catalog_insert=0x8d5bd928, stock_gui_role=0x8d5c3eac, stock_sample_classify_tail=0x8d5b79c6,
                   stock_sample_key_tail=0x8d589214)
TEMPLATES = 'sdk/platform/opxy-1.2.1/'
RUNTIME_ASM, RUNTIME_CXX = 'sdk/platform/blackfin/divide.s', 'sdk/platform/blackfin/memory.cpp'
ALLOWED_SECTIONS = {'.text', '.rodata', '.data', '.bss'}
MAX_COMPILER_FILE = 64 << 20
ANALYSIS = ['analysis/compile-report.json']
# Freestanding: no C++ runtime, no static constructors, no unwind tables.
CXX_FLAGS = ['-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables',
             '-fno-unwind-tables', '-fno-common', '-fno-tree-loop-distribute-patterns',
             '-fomit-frame-pointer', '-nostdinc++', '-Wall', '-Werror', '-Wno-unused-variable']
# Stock SVG parse (0x8d5f38ba) accepts at most four fractional digits and no exponents.
SVG_ELEMENTS = {'g', 'rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'text'}
SVG_NUMERIC = {'d', 'points', 'transform', 'x', 'y', 'x1', 'y1', 'x2', 'y2', 'cx', 'cy', 'r', 'rx', 'ry',
               'width', 'height', 'stroke-width', 'stroke-miterlimit', 'stroke-dasharray',
               'stroke-dashoffset', 'font-size', 'opacity', 'fill-opacity', 'stroke-opacity'}
NUMBER = r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)'
SVG_INHERITED = {'fill', 'stroke', 'stroke-width', 'font-family', 'font-size', 'style'}


def configuration(entries=None, layout=None):
    """The profile, the collection's machines numbered by the layout, and their link-time
    IDs. Entries are machines.read_collection's."""
    profile = config()
    stock_tables.check_profile(profile)
    require([p['kind'] for p in profile['patches']].count('startup') == 1, 'Expected one startup label edit')
    machines = collection(entries, layout)
    definitions = dict(DEFINITIONS)
    for m in machines:
        if m.kind == 'effect':
            definitions[m.prefix + '_fx_id'] = m.identity['effect']
            definitions[m.prefix + '_fx_page_id'] = m.identity['page']
        else:
            definitions[m.prefix + '_engine_id'] = m.identity['engine']
            definitions[m.prefix + '_page_id'] = m.identity['page']
    return profile, machines, definitions


def provenance(machines):
    """Every authored, SDK and compiler-profile input; private artifacts never enter.
    The collection is its machines, in order: their IDs and sources below."""
    paths = {PROFILE, LINKER}
    for folder in ('sdk/include', 'sdk/platform', 'tooling'):
        paths |= {p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob('*')
                  if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.md'}
    hashes = {name: sha(read(child(ROOT, name))) for name in paths}
    for m in machines:
        hashes.update({name: sha(read(path)) for name, path in m.files().items()})
    hashes['collection'] = sha(json.dumps([m.id for m in machines]).encode('utf-8'))
    return dict(sorted(hashes.items()))


def file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def compiler_paths(directory):
    """Every compiler file that can influence object bytes, hash-checked first."""
    directory = Path(directory).resolve()
    pins, suffix = host_pins('compiler')
    result = {}
    for relative, expected in pins.items():
        path = child(directory, relative)
        require(path.is_file() and path.stat().st_size <= MAX_COMPILER_FILE, f'Missing/oversized compiler file: {path}')
        require(file_hash(path) == expected, f'Compiler file hash mismatch: {relative}')
        result[relative] = path
    return dict(cxx=result['bin/bfin-elf-g++' + suffix])


def check_sources(machines):
    for m in machines:
        for path in tree(m.root):
            if path.suffix in ('.cpp', '.cc', '.h', '.hpp', '.inc') and 'vendor' not in path.relative_to(m.root).parts:
                validate_source_boundary(path.read_text(encoding='utf-8'), m.source(path.relative_to(m.root).as_posix()))


def compile_cxx(cxx, assembler, source, obj, includes=()):
    """cc1plus via the driver to assembly, then the pinned assembler.

    The driver never chooses an assembler, so object bytes depend only on the
    hash-checked compiler files and the pinned binutils. Repository sources are
    passed relative to the root so STT_FILE symbols match across rebuilds.
    """
    obj = Path(obj).resolve()
    assembly = obj.with_suffix('.s')
    generated = Path(source).is_absolute()
    flags = [*CXX_FLAGS, *('-I' + str((ROOT / path).resolve()) for path in includes), '-fstack-usage']
    subprocess.run([str(cxx), *flags, '-S', Path(source).name if generated else source, '-o', str(assembly)],
                   cwd=Path(source).parent if generated else ROOT, check=True, capture_output=True, text=True,
                   timeout=300)
    execute([assembler, '-mcpu=bf707', '-o', obj, assembly])
    return obj


def includes(m=None):
    """Include path for firmware-compiler units: SDK headers, then the machine's own folders."""
    paths = ['.', 'sdk/include', 'sdk/platform/opxy-1.2.1']
    if m is not None:
        paths.append(m.source())
    paths.append('sdk/platform/blackfin/freestanding')
    if m is not None:
        paths += [m.source(extra) for extra in m.includes]
    return paths


def plan(machines, profile=None):
    """Every object in link order as (name, owner machine or None, step).

    Each machine's objects come first, in collection order, then the SDK's
    shared runtime and startup, less units of profile services no machine uses.
    The browser kit links in this same order.
    """
    units = []
    for m in machines:
        if m.kind == 'synth':
            steps = ('voice', *('source:' + s for s in m.sources), 'display', 'screen', 'registration')
        elif m.kind == 'effect':
            steps = ('registration', 'fx_object')
        else:
            steps = ('registration', 'sample_voice')
        units += [(f'{m.id}/{Path(step.removeprefix("source:")).stem}.o', m, step) for step in steps]
    services = active_services(profile or config(), (m.kind for m in machines))
    units += [(f'sdk/{step}.o', None, step) for step in SDK_UNITS
              if UNIT_SERVICES.get(step) is None or UNIT_SERVICES[step] in services]
    names = [name for name, _m, _step in units]
    require(len(names) == len(set(names)), 'Duplicate object name')
    return units


# The SDK's shared objects, after every machine's. Sources are TEMPLATES/<unit>.cpp
# or .s, except the generated units and the Blackfin runtime.
SDK_UNITS = ('divide', 'memory', 'registration', 'registration_stock', 'startup', 'startup_exit', 'startup_label', 'benchmark', 'benchmark_clock', 'voice_clock',
             'hook', 'sample_machines', 'sample_hooks', 'sample_callbacks', 'sample_cache', 'sample_cache_stock',
             'sample_cache_hooks', 'sample_progress', 'sample_progress_stock', 'pad_assignment',
             'pad_assignment_stock', 'drum_page', 'drum_controls_stock', 'drum_display_stock', 'screen_order',
             'screen_hooks', 'synth_controls', 'synth_control_table', 'note_activity', 'project_companion',
             'project_companion_table', 'profiling', 'lfo', 'lfo_stock', 'lfo_hooks', 'stock_browser', 'punch', 'punch_example')
# SDK units that exist only for a profile service, linked when a machine needs it.
UNIT_SERVICES = dict.fromkeys(('sample_machines', 'sample_hooks', 'sample_callbacks', 'sample_cache',
                               'sample_cache_stock', 'sample_cache_hooks', 'sample_progress',
                               'sample_progress_stock', 'pad_assignment', 'pad_assignment_stock', 'drum_page',
                               'drum_controls_stock', 'drum_display_stock'), 'samples')


def module_bytes(profile):
    """Memory every selected machine and the SDK share: the reservation below the relocated stock tables."""
    return profile['reserved_bytes'] - profile['table_bytes']


def object_memory(data):
    """(initialized, zero-initialized) bytes a relocatable ELF32 object places in memory."""
    initialized = state = 0
    for _at, (_name, kind, flags, _address, _offset, size, *_rest) in elf_sections(data):
        if flags & 2:  # SHF_ALLOC
            if kind == 8:  # SHT_NOBITS
                state += size
            else:
                initialized += size
    return initialized, state


def memory_by_owner(directory, machines, profile=None):
    """Bytes each machine (and 'sdk' for shared code) places in the module, before alignment."""
    usage = {}
    for name, owner in object_owners(machines, profile).items():
        initialized, state = object_memory(read(Path(directory) / name))
        share = usage.setdefault(owner or 'sdk', dict(initialized_bytes=0, state_bytes=0))
        share['initialized_bytes'] += initialized
        share['state_bytes'] += state
    return usage


def memory_summary(usage, total, limit):
    shares = sorted(((owner, sum(share.values())) for owner, share in usage.items()), key=lambda item: -item[1])
    padding = total - sum(size for _owner, size in shares)
    if padding > 0:
        shares.append(('alignment', padding))
    return f'{total / 1024:.1f} of {limit / 1024:.0f} KiB: ' + ', '.join(
        f'{owner} {size / 1024:.1f} KiB' for owner, size in shares)


def object_owners(machines, profile=None):
    """Object name -> owning machine ID (None for the SDK's shared objects), in link order."""
    return {name: (m.id if m else None) for name, m, _step in plan(machines, profile)}


def object_services(machines, profile=None):
    """SDK object name -> the profile service it implements, for objects that implement one."""
    return {name: UNIT_SERVICES[step] for name, m, step in plan(machines, profile)
            if m is None and step in UNIT_SERVICES}


def compile_collection(directory, toolchain, compiler, profile, machines, definitions, version, profiling=True):
    """Compile and link every machine, labelled with the release version; return the
    module bytes, report and patched profile."""
    require(type(profiling) is bool, 'Profiling must be a boolean')
    directory = Path(directory)
    for m in machines:
        if m.display != 'stock':
            validate_panel(m)
    check_sources(machines)
    tools = tool_paths(toolchain)
    gcc = compiler_paths(compiler)
    require(sha(read(ROOT / LINKER)) == profile['linker_sha256'], 'Unreviewed memory layout')
    before = provenance(machines)
    effects, sample_engines = by_kind(machines, 'effect'), by_kind(machines, 'sample')

    def assemble_unit(source, obj):
        execute([tools['as'], '-mcpu=bf707', '-I', ROOT, '-o', obj, source])

    def cxx(source, obj, m=None):
        compile_cxx(gcc['cxx'], tools['as'], source, obj, includes(m))

    def display_unit(folder, m):
        generate_display(folder, machine=m)
        return folder / 'display_binding.cpp'

    def build_unit(obj, m, step):
        folder = obj.parent
        folder.mkdir(exist_ok=True)
        if m is None:
            if step == 'benchmark':
                return cxx(str(benchmark.generate(folder, machines)), obj)
            if step == 'screen_order':
                return cxx(str(generate_screen_order(folder)), obj)
            if step == 'sample_callbacks':
                return cxx(str(generate_sample_callbacks(folder, machines)), obj)
            if step == 'synth_control_table':
                return cxx(str(generate_synth_controls(folder, machines)), obj)
            if step == 'project_companion_table':
                return cxx(str(generate_project_companion_table(folder, machines)), obj)
            if step == 'registration_stock':
                return cxx(str(generate_registration_stock(folder)), obj)
            if step == 'divide':
                return assemble_unit(ROOT / RUNTIME_ASM, obj)
            if step == 'memory':
                return cxx(RUNTIME_CXX, obj)
            if step == 'startup_label':
                return assemble_unit(startup_label.generate(folder, version), obj)
            if step == 'startup':
                prefixes = [x.prefix for x in by_kind(machines, 'synth', 'sample')]
                return cxx(str(generate_startup(folder, prefixes, [x.prefix for x in effects],
                                                [x.prefix for x in sample_engines])), obj)
            if (ROOT / TEMPLATES / (step + '.cpp')).is_file():
                return cxx(TEMPLATES + step + '.cpp', obj)
            return assemble_unit(ROOT / TEMPLATES / (step + '.s'), obj)
        if step.startswith('source:'):
            return cxx(m.source(step.removeprefix('source:')), obj, m)
        source = {'voice': lambda: generate_voice(folder, m), 'display': lambda: display_unit(folder, m),
                  'screen': lambda: generate_screen(folder, m), 'fx_object': lambda: generate_fx_object(folder, m, benchmark=True),
                  'registration': lambda: generate_registration(folder, m),
                  'sample_voice': lambda: generate_sample_voice(folder, m, benchmark=True)}[step]
        return cxx(str(source()), obj, m)

    names = []
    for name, m, step in plan(machines, profile):
        obj = directory / name
        build_unit(obj, m, step)
        # Disable string pooling so the browser linker places sections exactly as GNU ld does.
        obj.write_bytes(normalize_object(read(obj)))
        require('R_BFIN_RIMM16' not in execute([tools['objdump'], '-r', obj]), 'Truncated address relocation in ' + name)
        names.append(name)
    stack_usage(directory)
    # GNU ld synthesizes FILE symbols from object arguments; relative names keep
    # independent rebuilds byte-identical.
    link = [tools['ld'], '-T', ROOT / LINKER, '-Map=module.map', '-o', 'module.elf',
            *(f'--defsym={key}={value:#x}' for key, value in definitions.items()), *names]
    try:
        subprocess.check_output([str(arg) for arg in link], cwd=directory, stderr=subprocess.STDOUT, text=True, timeout=120)
    except subprocess.CalledProcessError as error:
        overflow = re.search(r"region `MODULE' overflowed by (\d+) bytes", error.output or '')
        if not overflow and 'Module overlaps the relocated tables' not in (error.output or ''):
            raise
        usage, limit = memory_by_owner(directory, machines, profile), module_bytes(profile)
        total = limit + int(overflow.group(1)) if overflow else sum(sum(share.values()) for share in usage.values())
        raise ValueError('Machines exceed the shared module memory, ' + memory_summary(usage, total, limit)) from None
    module, sizes, patch_profile, hooks, stack, usage = check_module(directory, tools, profile, machines, profiling)
    require(provenance(machines) == before, 'Sources changed during compilation')
    report = dict(schema=1, target=TARGET, status='experimental-unqualified', sources=before,
                  host=host_platform(), tools=host_pins('tools')[0], compiler=host_pins('compiler')[0],
                  cxx_flags=CXX_FLAGS, profiling=profiling, benchmark_cases=benchmark.cases(machines),
                  sizes=sizes, memory=usage, module_bytes=module_bytes(profile), audio_stack_bytes=stack,
                  reserved_bytes=profile['reserved_bytes'],
                  module_sha256=sha(module), elf_sha256=sha(read(directory / 'module.elf')), hooks=hooks,
                  objects=object_owners(machines, profile), patches=patch_profile['patches'],
                  services=sorted(active_services(profile, (m.kind for m in machines))), device_operations=False,
                  hardware_tested=False, cpu_timing_measured=False)
    save_json(directory / 'compile-report.json', report)
    return bytes(module), report, patch_profile


def normalize_object(data):
    """Disable GNU string pooling and COMDAT groups for reproducible, simple section placement.

    An inline function the compiler keeps out of line, or a static inside one, comes
    in a COMDAT group with a weak symbol. Each object is one translation unit, so its
    sections become plain ones and its weak definitions global: a definition repeated
    across objects then fails both linkers alike instead of being merged by one.
    Both linkers receive these exact bytes. No instructions or relocations change.
    """
    sections = elf_sections(data)
    require(struct.unpack_from('<H', data, 16)[0] == 1, 'Expected Blackfin ET_REL')
    out = bytearray(data)
    for at, (_name, kind, flags, _addr, offset, size, *_rest) in sections:
        if kind == 17:  # SHT_GROUP: deactivate the header without moving section indices
            # PROGBITS leaves multiple orphan .group sections in GNU's final ELF.
            # SHT_NULL makes the linker ignore the obsolete group records entirely.
            struct.pack_into('<II', out, at + 4, 0, 0)
            continue
        struct.pack_into('<I', out, at + 8, flags & ~(0x30 if flags & 2 else 0) & ~0x200)
        if kind == 2:  # the symbol table: defined weak symbols become global
            for symbol in range(offset, offset + size, 16):
                if out[symbol + 12] >> 4 == 2 and struct.unpack_from('<H', out, symbol + 14)[0]:
                    out[symbol + 12] = 0x10 | out[symbol + 12] & 15
    return bytes(out)


def stack_usage(directory):
    worst = {}
    for path in Path(directory).rglob('*.su'):
        for line in path.read_text(encoding='utf-8').splitlines():
            location, size, kind = line.rsplit('\t', 2)
            name = location.split(':', 3)[-1]
            require(kind == 'static', 'Dynamic stack allocation in ' + name)
            worst[name] = int(size)
    return worst


_FUNCTION = re.compile(r'^([0-9a-f]{8}) <([^>]+)>:$')
_INSTRUCTION = re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)$')
_DIRECT = re.compile(r'(?:CALL|JUMP)(?:\.[LSXA])?\s+0x([0-9a-f]+)')


def _frame_delta(text):
    """Bytes an instruction pushes (+) or pops (-) on SP; 0 otherwise."""
    def registers(spec):
        return sum((7 if kind == 'R' else 5) - int(low) + 1 for kind, low in re.findall(r'([RP])(?:7|5):(\d)', spec))
    if m := re.match(r'\[--SP\] = \((.*)\);', text):
        return 4 * registers(m.group(1))
    if text.startswith('[--SP] ='):
        return 4
    if m := re.match(r'\((.*)\) = \[SP\+\+\];', text):
        return -4 * registers(m.group(1))
    if re.match(r'\w+ = \[SP\+\+\];', text):
        return -4
    if m := re.match(r'LINK 0x([0-9a-f]+);', text):
        return int(m.group(1), 16) + 8
    if m := re.match(r'SP \+= (-?)0x([0-9a-f]+);', text):
        return int(m.group(2), 16) * (1 if m.group(1) else -1)
    return 0


def audio_stack_bounds(disassembly, module, base, symbols, machines):
    """Worst-case stack bytes of each machine's audio entry; recursion fails the build.

    Frames are the deepest SP excursion along each function (LINK counts its
    saved RETS/FP). Edges are direct calls and tail jumps; an indirect call may
    reach any function whose address is stored in module data (except the SDK
    method tables, which only stock calls). Indirect calls are expanded one
    level: the bound covers a dispatch through a table, not a chain of them.
    """
    starts, body, current = [], {}, None
    for line in disassembly.splitlines():
        if m := _FUNCTION.match(line):
            current = int(m.group(1), 16)
            starts.append(current)
            body[current] = []
        elif current is not None and (m := _INSTRUCTION.match(line)):
            body[current].append(m.group(2).split('/*')[0].strip())
    starts.sort()
    owner = lambda address: max((s for s in starts if s <= address), default=None)
    tables = [(v['value'], v['value'] + v['size']) for name, v in symbols.items() if name.endswith('vtable')]
    word = lambda at: int.from_bytes(module[at - base:at - base + 4], 'little')
    taken = {w for at in range(base, base + len(module) - 3, 4)
             if not any(lo <= at < hi for lo, hi in tables) and (w := word(at)) in body}
    memo, active = {}, set()

    def depth(function, indirect=True):
        key = (function, indirect)
        if key in memo:
            return memo[key]
        require(key not in active, 'Recursive call path from an audio callback at %#x' % function)
        active.add(key)
        frame = deepest = 0
        callees = set()
        for text in body[function]:
            frame += _frame_delta(text)
            deepest = max(deepest, frame)
            if re.search(r'(?:CALL|JUMP) \(P\d\)', text):
                if indirect:
                    callees |= {(target, False) for target in taken}
            elif (m := _DIRECT.search(text)) and (target := owner(int(m.group(1), 16))) is not None:
                if target != function or text.startswith('CALL'):
                    callees.add((target, indirect))
        result = deepest + max((depth(*callee) for callee in callees), default=0)
        active.discard(key)
        memo[key] = result
        return result

    bounds = {}
    for m in machines:
        table, slot = (f'_{m.prefix}_fx_vtable', 0x10) if m.kind == 'effect' else (f'_{m.prefix}_vtable', 8)
        entry = word(symbols[table]['value'] + slot)
        require(entry in body, 'Audio entry of ' + m.id + ' is not a function start')
        bounds[m.id] = depth(entry)
    return bounds


def check_module(directory, tools, profile, machines, profiling=True):
    require(type(profiling) is bool, 'Profiling must be a boolean')
    elf = directory / 'module.elf'
    sections, symbols = elf32(read(elf))
    allocated = {name: s for name, s in sections.items() if s['flags'] & 2 and s['size']}
    require(set(allocated) <= ALLOWED_SECTIONS and {'.text', '.rodata', '.bss'} <= set(allocated),
            'Unexpected allocated sections: ' + ', '.join(sorted(allocated)))
    require(not any(name.startswith(('.init_array', '.fini_array', '.ctors', '.dtors')) and s['size']
                    for name, s in sections.items()), 'Static constructors are not supported')
    intervals = sorted((s['address'], s['address'] + s['size']) for s in allocated.values())
    require(all(a[1] <= b[0] for a, b in zip(intervals, intervals[1:])), 'Overlapping sections')
    low = profile['module_base']
    end = symbols['__extensions_initialized_end']['value']
    require(intervals[0][0] == low and low < end <= allocated['.bss']['address'] and
            intervals[-1][1] <= low + profile['reserved_bytes'] and allocated['.bss']['type'] == 8,
            'Module or BSS outside single shared reservation')
    module = bytearray(end - low)
    for name in ('.text', '.rodata', '.data'):
        if name in allocated:
            s = allocated[name]
            at = s['address'] - low
            require(0 <= at and at + s['size'] <= len(module), 'Section outside module')
            module[at:at + s['size']] = s['data']
    execute([tools['objcopy'], '-O', 'binary', elf, directory / 'module.bin'])
    require(read(directory / 'module.bin') == module, 'Independent ELF extraction disagrees')
    disassembly = execute([tools['objdump'], '-h', '-dr', elf])
    # The startup label thunk uses Blackfin+ absolute CALL/JUMP forms objdump
    # cannot decode; its exact bytes are checked against the stock targets.
    label = symbols[startup_label.SYMBOL]
    undecoded = [int(line.split(':')[0], 16) for line in disassembly.splitlines()
                 if 'ILLEGAL' in line and re.match(r'\s*[0-9a-f]+:', line)]
    require(all(label['value'] <= at < label['value'] + label['size'] for at in undecoded) and
            'R_BFIN_' not in disassembly, 'Invalid or unresolved instructions')
    require(startup_label.thunk_ok(read(directory / 'module.bin'), label['value'] - profile['module_base'],
                                   label['size'], symbols['kit_startup_label_text']['value']),
            'Unexpected startup label thunk')
    (directory / 'disassembly.txt').write_text(disassembly, encoding='utf-8')
    stack = audio_stack_bounds(disassembly, module, low, symbols, machines)
    sizes = dict(initialized_bytes=len(module), bss_bytes=allocated['.bss']['size'],
                 allocated_span_bytes=intervals[-1][1] - low)
    limit = module_bytes(profile)
    require(all(sizes.values()), 'Empty module section')
    require(symbols['__extensions_reserved_end']['value'] - low == limit, 'Linker module region differs from the profile')
    usage = memory_by_owner(directory, machines, profile)
    # One pool for code, data and state of every machine; name each share when it overflows.
    if sizes['allocated_span_bytes'] > limit:
        raise ValueError('Machines exceed the shared module memory, ' +
                         memory_summary(usage, sizes['allocated_span_bytes'], limit))
    placed = [symbols['__stock_%s' % t.name.replace(' ', '_')]['value'] for t in stock_tables.TABLES]
    require(placed == [t.address for t in stock_tables.TABLES] and
            max(t.end for t in stock_tables.TABLES) <= low + profile['reserved_bytes'],
            'Relocated tables outside the reserved tail or overlapping the module')
    patch_profile = copy.deepcopy(profile)
    hooks = {}
    text = allocated['.text']
    services = active_services(profile, (m.kind for m in machines))
    for patch in patch_profile['patches']:
        if not patch_active(patch, services, profiling):
            patch['after'] = patch['before']  # inactive hooks retain stock code
        elif patch['kind'] == 'startup':
            patch['after'] = startup_label.jump(symbols[patch['symbol']]['value']).hex()
        elif patch['kind'] == 'hook':
            symbol = symbols[patch['symbol']]
            require(symbol['type'] == 2 and symbol['size'] > 0 and text['address'] <= symbol['value'] and
                    symbol['value'] + symbol['size'] <= text['address'] + text['size'],
                    'Hook must resolve to a bounded module function')
            patch['after'] = hook_instruction(patch, symbol['value']).hex()
            require(hook_target(patch, bytes.fromhex(patch['after'])) == (symbol['value'], patch['call']),
                    'Branch encoding mismatch')
            hooks[patch['symbol']] = symbol['value']
    check_patch_boundaries(patch_profile, len(profile['patches']))
    return module, sizes, patch_profile, hooks, stack, usage


def display_ids(prefix, display):
    """Artwork IDs of a display: its nodes (the first are the mutable paths), its
    text slots, then its palette entries."""
    return ([f'{prefix}_node_{i}' for i in range(display['nodes'])] +
            [f'{prefix}_text_{i}' for i in range(display['texts'])] +
            [f'{prefix}_palette_{i}' for i in range(display['palette'])])


def panel_root(fragment, source):
    """An artwork fragment: one well-formed root element, such as <g id="<prefix>_panel">."""
    try:
        return ET.fromstring(fragment)
    except ET.ParseError as error:
        raise ValueError(f'{source} must be one SVG element, such as <g id="..._panel">...</g> ({error})') from None


def check_outlines(by_id, prefix, display):
    """Drawing paths are the first nodes: exactly M, (points - 1) L and Z, unfilled."""
    points = display['points']
    for name in display_ids(prefix, display)[:display['paths']]:
        node = by_id.get(name)
        commands = re.findall(r'[A-Za-z]', node.get('d', '')) if node is not None else []
        coordinates = re.findall(NUMBER, node.get('d', '')) if node is not None else []
        require(node is not None and node.tag == 'path' and commands == ['M'] + ['L'] * (points - 1) + ['Z'] and
                len(coordinates) == 2 * points and node.get('fill') == 'none',
                'Mutable outline must contain exactly M, %d L and Z, with no fill: %s' % (points - 1, name))
        # The stock parser merges repeated points, leaving fewer than the frame
        # binds, and the device then no longer reshapes the outline. Points are
        # compared as the Q10 coordinates the device holds.
        pairs = [(round(float(x) * 1024), round(float(y) * 1024)) for x, y in zip(coordinates[::2], coordinates[1::2])]
        require(len(set(pairs)) == points,
                'Mutable outline repeats a point; the device merges repeated points, so keep each distinct: ' + name)


def check_bindings(by_id, prefix, display):
    """The display's nodes, text slots and outlines in the artwork; returns its palette IDs.
    Every frame sets a bound element's whole transform, so the authored one is a
    plain translation or none: the pose before the first frame."""
    ids = display_ids(prefix, display)
    count = display['nodes'] + display['texts']
    bound, texts, palette = ids[:display['nodes']], ids[display['nodes']:count], ids[count:]
    check_outlines(by_id, prefix, display)
    for name in bound + texts:
        node = by_id.get(name)
        kind = ('text',) if name in texts else ('path', 'circle')
        require(node is not None and node.tag in kind, 'Authored SVG is missing %s %s' % ('/'.join(kind), name))
        require(re.fullmatch(r'(matrix\(1 0 0 1 (%s) (%s)\))?' % (NUMBER, NUMBER), node.get('transform', '')),
                'Bound elements take transform="matrix(1 0 0 1 x y)" or none: ' + name)
    for name in texts:
        node = by_id[name]
        require(not {'x', 'y'} & set(node.keys()) and len(node.text or '') < 24,
                'Text slots are positioned by their transform and hold at most 23 characters: ' + name)
    return palette


def validate_panel(m, fragment=None):
    """The authored SVG subset plus the display contract's required node bindings."""
    fragment = read(m.root / 'panel.svg.fragment') if fragment is None else fragment
    nodes = list(panel_root(fragment, m.source('panel.svg.fragment')).iter())
    ids = [node.get('id') for node in nodes if node.get('id')]
    require(len(ids) == len(set(ids)), 'Duplicate authored SVG ID')
    by_id = {node.get('id'): node for node in nodes if node.get('id')}

    def visible(name, tags=('path', 'circle')):
        node = by_id.get(name)
        require(node is not None and node.tag in tags and node.get('fill') not in (None, 'none') and
                node.get('stroke') not in (None, 'none') and float(node.get('stroke-width', '0')) > 0,
                'Bound node needs visible explicit fill and stroke: ' + name)

    def donor(name):
        # Renderers copy donor paint verbatim: explicit #RRGGBB fill and stroke.
        visible(name)
        require(all(re.fullmatch(r'#[0-9A-Fa-f]{6}', by_id[name].get(k)) for k in ('fill', 'stroke')),
                'Palette donors need #RRGGBB fill and stroke: ' + name)

    palette = check_bindings(by_id, m.prefix, m.definition['resources']['display'])
    for name in palette:
        donor(name)
    for node in nodes:
        for attribute in SVG_NUMERIC.intersection(node.attrib):
            value = node.attrib[attribute]
            require(not re.search(r'\d[eE][-+]?\d', value), 'Exponent notation is outside the authored SVG numeric subset')
            require(not re.search(r'\.\d{5,}', value), 'Authored SVG numbers must have at most four decimal places')
        require(node.tag in SVG_ELEMENTS, 'Unsupported authored SVG element')
        require('style' not in node.attrib and not any(k.lower().startswith('on') or 'href' in k.lower()
                for k in node.attrib), 'Unsupported SVG style, event or external reference')
        if node.tag == 'g':
            require(not SVG_INHERITED.intersection(node.attrib), 'Put SVG paint/font attributes on leaves')
        else:
            require('fill' in node.attrib, 'Missing explicit SVG fill')
            if node.tag == 'text':
                require({'font-family', 'font-size'} <= node.attrib.keys(), 'Missing explicit text font')
            if node.get('fill') == 'none':
                require(node.get('stroke') not in (None, 'none') and float(node.get('stroke-width', '0')) > 0,
                        'Invisible authored SVG outline')
    return fragment


def extend_catalog(catalog, profile, machines):
    """Append each machine's entry to its stock list; nothing else changes.

    The insertion offsets are bound to the exact authenticated stock catalog.
    """
    groups = dict(engine_types=by_kind(machines, 'synth', 'sample'), fx_types=by_kind(machines, 'effect'))
    result, base = catalog, json.loads(catalog)
    for key, at in sorted(profile['stock_catalog_inserts'].items(), key=lambda item: -item[1]):
        result = result[:at] + ''.join(catalog_entry(m) for m in groups[key]).encode('utf-8') + result[at:]
    added = {key: [json.loads(catalog_entry(m).strip().lstrip(',')) for m in group] for key, group in groups.items()}
    require(all(len(entry['default_params']) == 8 for group in added.values() for entry in group), 'Invalid catalog entry')
    require(json.loads(result) == dict(base, **{key: base[key] + added[key] for key in groups}),
            'Unexpected catalog modification')
    return result


def prepare_assets(catalog, profile, machines):
    require(sha(catalog) == profile['stock_catalog_sha256'], 'Stock catalog hash mismatch')
    outputs = {'content/op1_factory.json': extend_catalog(catalog, profile, machines)}
    for m in machines:
        if m.display != 'stock':
            outputs[f'content/display/{m.native_name}.svg'] = screen.compose(validate_panel(m))
    return outputs


def package_collection(package):
    """The collection a package was built from, as its manifest records it."""
    entries = document(Path(package) / 'manifest.json').get('collection')
    require(isinstance(entries, list) and entries, 'Invalid package collection')
    return entries


def package_version(package):
    """The release version a package was built as, as its manifest records it."""
    return startup_label.checked(document(Path(package) / 'manifest.json').get('version', startup_label.UNVERSIONED))


def package_layout(package):
    """The layout a package's machines were numbered from, as its manifest records it."""
    layout = document(Path(package) / 'manifest.json').get('layout')
    require(layout is None or isinstance(layout, dict), 'Invalid package layout')
    return layout


def numbers_after(layout, machines):
    """Every number a layout holds once these machines are built: its own, the fixed
    numbers and the machines'."""
    numbers = dict(layout or {})
    numbers.update(FIXED_NUMBERS)
    numbers.update({m.id: m.identity for m in machines})
    return numbers


def verification(stock, catalog, package, toolchain, compiler, *, require_record=True):
    manifest = document(package / 'manifest.json')
    require(manifest.get('schema') == 1 and manifest.get('target') == TARGET, 'Unexpected manifest')
    profiling = manifest.get('profiling')
    require(type(profiling) is bool, 'Manifest profiling must be a boolean')
    profile, machines, definitions = configuration(package_collection(package), package_layout(package))
    with tempfile.TemporaryDirectory(prefix='opxy-verify-') as temporary:
        temporary = Path(temporary)
        module, report, patch_profile = compile_collection(temporary, toolchain, compiler, profile, machines,
                                                           definitions, package_version(package), profiling)
        require(report['profiling'] is profiling, 'Compile profiling differs from manifest')
        expected_assets = prepare_assets(catalog, profile, machines)
        expected_assets['os.ldr'] = assemble(stock, module, patch_profile, profile, len(profile['patches']),
                                               set(report['services']), profiling)
        expected = {'candidate/' + name: data for name, data in expected_assets.items()}
        expected['analysis/module.bin'] = module
        expected['analysis/module.elf'] = read(temporary / 'module.elf')
        require(document(package / 'analysis/compile-report.json') == report, 'Compile provenance differs from fresh build')
        require(set(manifest.get('files', {})) == set(expected) | set(ANALYSIS), 'Unexpected manifest file set')
        for name, data in expected.items():
            require(read(child(package, name)) == data, 'Rebuilt bytes differ: ' + name)
        for name, digest in manifest['files'].items():
            require(sha(read(child(package, name))) == digest, 'Package hash mismatch: ' + name)
        actual = {p.relative_to(package).as_posix() for p in (package / 'candidate').rglob('*') if p.is_file()}
        require(actual == {p for p in expected if p.startswith('candidate/')}, 'Unexpected deployment file')
    result = dict(target=TARGET, status='experimental-unqualified', os_sha256=sha(expected_assets['os.ldr']),
                  machines=[m.source() for m in machines],
                  fresh_source_rebuild_matches=True, original_entry_preserved=True,
                  profiling=profiling,
                  exact_edits=sum(patch_active(p, set(report['services']), profiling) for p in profile['patches']),
                  single_shared_reservation_bytes=profile['reserved_bytes'],
                  sizes=report['sizes'], device_operations=False, hardware_tested=False, cpu_timing_measured=False,
                  brick_risk_eliminated=False)
    if require_record:
        require(document(package / 'verification.json') == result, 'Verification record differs from recomputed result')
    return result


def build(out, toolchain, compiler, stock_os, stock_content, entries=None, layout=None, profiling=True):
    """A firmware package. Its machines take their numbers from `layout` (by machine ID);
    the package's layout.json is that layout after this build, for the next one."""
    require(type(profiling) is bool, 'Profiling must be a boolean')
    entries = [] if entries is None else entries
    profile, machines, definitions = configuration(entries, layout)
    require(not any(m.contains(out) for m in machines), 'Keep build output outside every machine folder')
    stock = read(stock_os)
    require(sha(stock) == profile['stock_sha256'], 'Stock OS hash mismatch')
    catalog = read(Path(stock_content) / 'op1_factory.json')
    prepared = prepare_assets(catalog, profile, machines)
    output = fresh_output(out)
    analysis = output / 'analysis'
    analysis.mkdir()
    version = startup_label.release_version()
    module, report, patch_profile = compile_collection(analysis, toolchain, compiler, profile, machines,
                                                       definitions, version, profiling)
    prepared['os.ldr'] = assemble(stock, module, patch_profile, profile, len(profile['patches']),
                                  set(report['services']), profiling)
    for name, data in prepared.items():
        path = child(output / 'candidate', name)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    tracked = ['candidate/' + name for name in prepared] + ['analysis/module.bin', 'analysis/module.elf'] + ANALYSIS
    save_json(output / 'manifest.json', dict(schema=1, target=TARGET, version=version, collection=entries, profiling=profiling,
                                             **({'layout': layout} if layout else {}),
                                             files={name: sha(read(output / name)) for name in tracked}))
    save_json(output / 'layout.json', layout_document(numbers_after(layout, machines)))
    result = verification(stock, catalog, output, toolchain, compiler, require_record=False)
    save_json(output / 'verification.json', result)
    return output, result


def add_collection_arguments(parser):
    parser.add_argument('--collection', type=Path,
                        help='collection file of {"path": ...} machine folders, such as local/catalog/collection.json '
                             'from python sdk.py catalog fetch')
    parser.add_argument('--machine', type=Path, action='append', default=[],
                        help='a machine folder to add to the collection; repeatable')


def selected_collection(args):
    return read_collection(args.collection, args.machine)


COMMANDS = {
    'compile': 'Compile and link the collection into the native module alone; needs no stock files.',
    'build': 'Build firmware: link the collection into stock OP-XY 1.2.1 and check the result.',
    'verify': 'Rebuild an existing package independently and compare it (release host only).',
}


def main(argv=None, command=None):
    command = command or 'build'
    parser = argparse.ArgumentParser(prog='sdk.py ' + command, description=COMMANDS[command] + ' No device operations.')
    parser.add_argument('--toolchain', type=Path, help='pinned binutils directory (default: local/toolchain/bin)')
    parser.add_argument('--compiler', type=Path, help='pinned Blackfin GCC bundle root (default: local/toolchain)')
    if command == 'verify':
        parser.add_argument('--package', type=Path, required=True)
    else:
        parser.add_argument('--out', type=Path, required=True)
    if command != 'compile':
        parser.add_argument('--stock-os', type=Path, required=True)
        parser.add_argument('--stock-content', type=Path, default=STOCK_CONTENT,
                            help="the folder holding op1_factory.json (default: this repository's copy)")
    if command != 'verify':  # a package records its own collection
        parser.add_argument('--profiling', choices=('on', 'off'), default='on',
                            help='per-voice diagnostic timing (default: on for SDK development builds)')
        add_collection_arguments(parser)
        parser.add_argument('--layout', type=Path,
                            help="the machine numbers your OP-XY's projects use: the layout.json of your last build")
    args = parser.parse_args(argv)
    # A failed command removes only the --out folder it created itself.
    created = command != 'verify' and not args.out.exists()
    try:
        if command == 'verify':
            require_release_host()  # only its tools reproduce published package bytes
        if command != 'compile':
            require((args.stock_content / 'op1_factory.json').is_file(),
                    f'--stock-content must be the folder holding op1_factory.json '
                    f'(a copy of the device\'s /content): {args.stock_content}')
        # A package records the collection it was built from.
        entries = package_collection(args.package) if command == 'verify' else selected_collection(args)
        layout = read_layout(args.layout) if command != 'verify' and args.layout else None
        profile, machines, definitions = configuration(entries, layout)
        tools = resolve_toolchain(args.toolchain, args.compiler)
        tool_paths(tools[0])
        compiler_paths(tools[1])
        if command == 'compile':
            require(not any(m.contains(args.out) for m in machines), 'Keep build output outside every machine folder')
            output = fresh_output(args.out)
            _module, report, _patches = compile_collection(output, *tools, profile, machines, definitions,
                                                           startup_label.release_version(), args.profiling == 'on')
            print(json.dumps(report['sizes'], indent=2))
            print('Memory ' + memory_summary(report['memory'], report['sizes']['allocated_span_bytes'],
                                            report['module_bytes']))
            print('Non-bootable development module; not an OS image.')
        elif command == 'verify':
            stock, catalog = read(args.stock_os), read(args.stock_content / 'op1_factory.json')
            require(sha(stock) == profile['stock_sha256'], 'Stock OS hash mismatch')
            print(json.dumps(verification(stock, catalog, args.package.resolve(), *tools), indent=2))
        else:
            output, result = build(args.out, *tools, args.stock_os, args.stock_content, entries, layout,
                                   args.profiling == 'on')
            print(json.dumps(result, indent=2))
            print('Local experimental package: ' + str(output))
            candidate = output / 'candidate'
            print('Files to install (docs/build.md, step 5), in ' + str(candidate) + ':')
            for path in sorted(p for p in candidate.rglob('*') if p.is_file()):
                print('  ' + path.relative_to(candidate).as_posix())
            print('Machine numbers: ' + str(output / 'layout.json') + ' (pass it as --layout to your next build)')
        return 0
    except (ValueError, OSError, KeyError, ET.ParseError, subprocess.SubprocessError) as error:
        print('ERROR: ' + str(error), file=sys.stderr)
        for stream in ('output', 'stderr'):
            if isinstance(error, subprocess.CalledProcessError) and getattr(error, stream, None):
                print(getattr(error, stream), file=sys.stderr)
        if created and args.out.exists():
            shutil.rmtree(args.out)
        return 1
