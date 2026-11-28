"""Machine records (sdk/platform/opxy-1.2.1/machine_record.hpp).

Each machine's generated record.cpp defines <prefix>_record: its registration
entry points, its numbers and its row in every shared table it takes part in.
A build's one selection-specific object, machine_records.o, lists the selected
machines' records by kind, then ID (machines.record_order), and reserves the space
startup builds the shared tables in (machine_tables.cpp). No other object depends
on which machines a build holds, so a machine's objects link unchanged beside any
others: the browser builder writes the same object for its selection
(builder/core.mjs recordsObject).
"""
from pathlib import Path

from . import audio_telemetry, benchmark, track_stage
from .adapters import (CONTROL_USINGS, companion_row, drum_row, kit_row, lfo_row, model_edit_row, player_row, sample_row,
                       send_row, synth_control_row, track_control_row)
from .formats import require
from .machines import RECORD_KINDS, record_order
from .stock_tables import EFFECT_COUNT, ENGINE_COUNT

# The most table bytes one record's rows take (machine_record.hpp kRecordTableBytes):
# machine_records.o reserves this much per selected machine, and once more for the
# companion table's ending row. builder/core.mjs RECORD_TABLE_BYTES is the same.
RECORD_TABLE_BYTES = 192
OBJECT = 'sdk/machine_records.o'
# Each record member a table row fills, the row function, its C++ type and its header.
TABLES = (('sample', sample_row, 'opxy::platform::SampleMachine', 'sample_machines.hpp'),
          ('controls', synth_control_row, 'opxy::platform::SynthControls', 'synth_controls.hpp'),
          ('lfo', lfo_row, 'opxy::platform::LfoMachine', 'lfo.hpp'),
          ('companion', companion_row, 'opxy::platform::CompanionMachine', 'project_companion.hpp'),
          ('control', track_control_row, 'opxy::platform::ControlMachine', 'track_control.hpp'),
          ('player', player_row, 'opxy::platform::PlayerEntry', 'players.hpp'),
          ('drum', drum_row, 'opxy::platform::DrumMachine', 'drum_machines.hpp'),
          ('model_edit', model_edit_row, 'opxy::platform::DrumModelEdit', 'drum_model_edit.hpp'),
          ('kit', kit_row, 'opxy::platform::DrumKit', 'drum_kit_controls.hpp'),
          ('send', send_row, 'opxy::platform::DrumSendKit', 'drum_send.hpp'),
          ('stage', track_stage.stage_row, 'opxy::platform::track_stage::Stage', 'track_stage.hpp'),
          ('audio', audio_telemetry.owner_row, 'opxy::platform::telemetry::Owner', 'audio_owner.hpp'))


def symbol(m):
    """A machine's record symbol."""
    return m.prefix + '_record'


def startup_entries(m):
    """The record's startup members (machine_record.hpp) in order, with the symbols they name."""
    p, kind = m.prefix, m.kind
    own_screen = m.display != 'stock'
    if kind == 'player':
        return ('nullptr', p + '_screen_register', 'nullptr', 'nullptr', 'nullptr', 'nullptr', 'nullptr', 'nullptr')
    if kind == 'effect':
        return (p + '_fx_register', p + '_screen_register' if own_screen else 'nullptr', p + '_ready', 'nullptr',
                'nullptr', p + '_fx_id', p + '_fx_page_id' if own_screen else 'nullptr', 'nullptr')
    if kind == 'sample':
        return (p + '_register', p + '_screen_register' if own_screen else 'nullptr', p + '_ready', p + '_name_matches',
                p + '_screen_ready' if own_screen else 'nullptr', p + '_engine_id', 'nullptr',
                p + '_screen_page_id' if own_screen else 'nullptr')
    # A synth or kit always draws its own page, which guards its registration.
    require(own_screen, f'{m.id} has no page of its own')
    return (p + '_register', p + '_screen_register', p + '_ready', p + '_name_matches', 'nullptr', p + '_engine_id',
            p + '_page_id', 'nullptr')


def startup_declarations(entries):
    """The declarations of the symbols startup_entries names."""
    register, screen, ready, name, screen_ready, number, page, screen_page = entries
    declarations = [f'int {s}() __asm__("{s}");' for s in (register, screen, ready, screen_ready) if s != 'nullptr']
    if name != 'nullptr':
        declarations.append(f'int {name}(const void*) __asm__("{name}");')
    declarations += [f'extern const char {s}[] __asm__("{s}");' for s in (number, page, screen_page) if s != 'nullptr']
    return declarations


def render(m, sweep=False):
    """A machine's record.cpp: its record, and its rows in the shared tables and its diagnostic
    runs as data beside it. The same for a machine whatever else a build holds."""
    require(m.kind in RECORD_KINDS, 'Unknown machine kind ' + m.kind)
    entries = startup_entries(m)
    declarations, definitions, data, headers, usings = startup_declarations(entries), [], [], [], []
    rows = []
    for member, row_of, type_name, header in TABLES:
        row = row_of(m)
        if row is None:
            rows.append('nullptr')
            continue
        declarations += row.declarations
        if row.definitions:
            definitions.append(row.definitions)
            usings += CONTROL_USINGS
        headers.append(header)
        data.append(f'const {type_name} {member}_row = {row.value};')
        rows.append(f'&{member}_row')
    runs = benchmark.runs(m, sweep)
    if runs is None:
        rows += ['nullptr', '0', 'nullptr', '0', 'nullptr']
    else:
        run_declarations, run_rows, settings, defaults = runs
        declarations += run_declarations
        headers.append('benchmark.hpp')
        data.append('const opxy::benchmark::Run benchmark_runs[] = {\n' + ''.join(f'    {r},\n' for r in run_rows) + '};')
        if settings:
            data.append('const opxy::benchmark::Setting benchmark_settings[] = {\n' + ''.join(f'    {s},\n' for s in settings) + '};')
            data.append('const opxy::i32 benchmark_defaults[8] = {' + ', '.join(map(str, defaults)) + '};')
        rows += ['benchmark_runs', str(len(run_rows)), 'benchmark_settings' if settings else 'nullptr', str(len(settings)),
                 'benchmark_defaults' if settings else 'nullptr']
    unique = []
    for d in declarations:
        if d not in unique:
            unique.append(d)
    kind = RECORD_KINDS.index(m.kind)
    source = (f"// Generated: {m.id}'s record (machine_record.hpp): its registration and its rows in the SDK's\n"
              "// shared tables, which startup copies into them (machine_tables.cpp).\n" +
              ''.join(f'#include "{h}"\n' for h in ['machine_record.hpp', *dict.fromkeys(headers)]) +
              'extern "C" {\n' + ''.join(d + '\n' for d in unique) +
              f'extern const opxy::platform::MachineRecord {symbol(m)} __asm__("{symbol(m)}");\n}}\n' +
              ''.join(f'using {u};\n' for u in dict.fromkeys(usings)) + ''.join(d + '\n' for d in definitions) +
              'namespace {\n' + ''.join(d + '\n' for d in data) + '}\n' +
              f'const opxy::platform::MachineRecord {symbol(m)} = {{\n    {kind}, ' + ', '.join(entries) + ',\n    ' +
              ', '.join(rows) + '\n};\n')
    return source


def generate(directory, m, sweep=False):
    """Write machine `m`'s record.cpp into `directory`."""
    destination = Path(directory) / 'record.cpp'
    destination.write_text(render(m, sweep), encoding='utf-8', newline='\n')
    return destination


def space_bytes(count):
    """The table space machine_records.o reserves for `count` records."""
    return (count + 1) * RECORD_TABLE_BYTES


def assembly(machines):
    """machine_records.s: the selected machines' records in record order, their count and the
    space the shared tables take. builder/core.mjs recordsObject writes the same object."""
    ordered = record_order(machines)
    require(ordered and len({m.prefix for m in ordered}) == len(ordered), 'Duplicate machine prefix or no machines')
    # Startup registers them into the engine and effect records the firmware relocates.
    require(sum(m.kind in ('synth', 'drum', 'sample') for m in ordered) <= ENGINE_COUNT and
            sum(m.kind == 'effect' for m in ordered) <= EFFECT_COUNT, 'More machines than the firmware registers')
    space = space_bytes(len(ordered))
    lines = ['/* Generated: the selected machines\' records (machine_record.hpp), by kind then ID. */',
             '.section .rodata', '.p2align 2',
             '.global sdk_machine_record_count', 'sdk_machine_record_count:', f'    .long {len(ordered)}',
             '.global sdk_machine_record_space_bytes', 'sdk_machine_record_space_bytes:', f'    .long {space}',
             '.global sdk_machine_records', 'sdk_machine_records:',
             *(f'    .long {symbol(m)}' for m in ordered),
             '.section .bss', '.p2align 2',
             '.global sdk_machine_record_space', 'sdk_machine_record_space:', f'    .space {space}', '']
    return '\n'.join(lines)
