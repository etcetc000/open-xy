"""Machine records (tooling/records.py, machine_record.hpp, machine_tables.cpp): each machine's
record holds its rows, and startup copies them into the shared tables, so no object but the
selection's records object depends on which machines a build holds."""
import copy
import dataclasses
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling import records
from tooling.adapters import check_track_controls
from tooling.machines import record_order
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX
from machine_fixtures import fixture

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def variant(name, new_id, **resources):
    """Fixture `name` as machine `new_id`, with these resources added."""
    m = fixture(name)
    definition = copy.deepcopy(m.definition)
    definition['resources'].update(resources)
    return dataclasses.replace(m, id=new_id, definition=definition)


def every_row():
    """Machines that between them fill every table a record can: each kind, kit controls, model
    edits, per-key sends, track control, a track stage and a player, listed out of record order."""
    return [fixture('probepads'), fixture('probestage'), fixture('probefx'), fixture('probesample'), fixture('probekit23'),
            fixture('probekit25'), variant('probekit21', 'probemix', padMix=1), fixture('probesynth'),
            variant('probesources', 'probeaux', trackControl={'stateBytes': 8, 'stateAlignment': 4, 'unique': True,
                                                              'aux': [12]}),
            fixture('probecurves')]


# Each table's pointer and count (machine_tables.cpp) and the record member holding a machine's row.
TABLES = (('sample', 'sdk_sample_machines', 'sdk_sample_machine_count'),
          ('controls', 'sdk_synth_controls', 'sdk_synth_control_count'),
          ('lfo', 'sdk_lfo_machines', 'sdk_lfo_machine_count'),
          ('control', 'sdk_control_machines', 'sdk_control_machine_count'),
          ('player', 'sdk_players', 'sdk_player_count'),
          ('drum', 'sdk_drum_machines', 'sdk_drum_machine_count'),
          ('model_edit', 'sdk_drum_model_edits', 'sdk_drum_model_edit_count'),
          ('kit', 'sdk_drum_kits', 'sdk_drum_kit_count'),
          ('send', 'sdk_drum_send_kits', 'sdk_drum_send_kit_count'),
          ('stage', 'sdk_track_stages', 'sdk_track_stage_count'))

CHECK = r'''
#include <cstdio>
#include <cstring>
#include "machine_record.hpp"
#include "sample_machines.hpp"
#include "synth_controls.hpp"
#include "lfo.hpp"
#include "project_companion.hpp"
#include "track_control.hpp"
#include "players.hpp"
#include "drum_machines.hpp"
#include "drum_model_edit.hpp"
#include "drum_kit_controls.hpp"
#include "drum_send.hpp"
#include "track_stage.hpp"
using namespace opxy;
using namespace opxy::platform;
static int failures = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
// Every record's row for a table, in record order: the table's rows, byte for byte, in the space.
template <typename Row>
static void table(const char* name, const Row* MachineRecord::*member, const Row* rows, u32 count) {
    u32 expected = 0;
    for (u32 i = 0; i < sdk_machine_record_count; ++i) {
        const Row* row = sdk_machine_records[i]->*member;
        if (!row) continue;
        EXPECT(expected < count && std::memcmp(&rows[expected], row, sizeof(Row)) == 0);
        ++expected;
    }
    EXPECT(expected == count);
    const u8* at = reinterpret_cast<const u8*>(rows);
    EXPECT(count == 0 || (at >= sdk_machine_record_space &&
                          at + count * sizeof(Row) <= sdk_machine_record_space + sdk_machine_record_space_bytes));
    std::printf("%s %u\n", name, static_cast<unsigned>(count));
}
static void check() {
@TABLES@
    // The companion table: every record's row, by ID, then an empty one.
    u32 n = 0;
    for (; sdk_companion_machines[n].id; ++n) {
        if (n) EXPECT(std::strcmp(sdk_companion_machines[n - 1].id, sdk_companion_machines[n].id) < 0);
        bool found = false;
        for (u32 i = 0; i < sdk_machine_record_count; ++i)
            if (sdk_machine_records[i]->companion &&
                std::memcmp(sdk_machine_records[i]->companion, &sdk_companion_machines[n], sizeof(CompanionMachine)) == 0)
                found = true;
        EXPECT(found);
    }
    u32 companions = 0;
    for (u32 i = 0; i < sdk_machine_record_count; ++i) companions += sdk_machine_records[i]->companion != nullptr;
    EXPECT(n == companions);
    static const CompanionMachine empty = {};
    EXPECT(std::memcmp(&sdk_companion_machines[n], &empty, sizeof empty) == 0);
    std::printf("companion %u\n", static_cast<unsigned>(n));
}
int main() {
    EXPECT(sdk_companion_machines && !sdk_companion_machines[0].id);  // before the first build: none
    EXPECT(sdk_synth_control_count == 0);
    sdk_machine_tables();
    check();
    // A later build (the other role's) writes the same bytes at the same places.
    static u8 before[@COUNT@ * kRecordTableBytes];
    std::memcpy(before, sdk_machine_record_space, sizeof before);
    const SynthControls* controls = sdk_synth_controls;
    sdk_machine_tables();
    EXPECT(std::memcmp(before, sdk_machine_record_space, sizeof before) == 0 && controls == sdk_synth_controls);
    check();
    return failures;
}
'''


def records_source(machines):
    """The host's stand-in for machine_records.o (records.assembly): the same symbols."""
    ordered = record_order(machines)
    return ('#include "machine_record.hpp"\nusing namespace opxy;\nusing opxy::platform::MachineRecord;\nextern "C" {\n' +
            ''.join(f'extern const MachineRecord {records.symbol(m)} __asm__("{records.symbol(m)}");\n' for m in ordered) +
            'const MachineRecord* const sdk_machine_records[] = {' + ', '.join('&' + records.symbol(m) for m in ordered) +
            '};\n' + f'const u32 sdk_machine_record_count = {len(ordered)};\n'
            f'const u32 sdk_machine_record_space_bytes = {len(ordered) + 1} * opxy::platform::kRecordTableBytes;\n'
            f'alignas(8) u8 sdk_machine_record_space[{len(ordered) + 1} * opxy::platform::kRecordTableBytes];\n}}\n')


def stubs(sources):
    """Every machine symbol the records name (their own objects define them), as host data."""
    # Asm-labelled names, and a padMix kit's method table, which keeps its C name.
    pattern = r'__asm__\("([A-Za-z0-9_]+)"\)|extern const void\* const ([A-Za-z0-9_]+)\[\];'
    names = sorted({a or b for source in sources for a, b in re.findall(pattern, source)
                    if not (a or b).endswith('_record') and not (a or b).startswith('sdk_')})
    return ''.join(f'__asm__(".globl {n}\\n.data\\n.balign 4\\n{n}:\\n.long 0\\n");\n' for n in names) + '''
#include "synth_controls.hpp"
namespace opxy { namespace platform {
void control_tenths(char* text, u32, const char*) { text[0] = 0; }
u32 control_ratio(i32, const u32*, u32) { return 0; }
void control_label(char* text, i32, const char* const*, u32) { text[0] = 0; }
void control_division(char* text, i32) { text[0] = 0; }
} }
'''


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class RecordTables(unittest.TestCase):
    def build(self, machines, folder):
        folder = Path(folder)
        check_track_controls(machines)
        sources = []
        for m in machines:
            (folder / m.id).mkdir()
            sources.append(records.generate(folder / m.id, m))
        (folder / 'records.cpp').write_text(records_source(machines), encoding='utf-8')
        (folder / 'stubs.cpp').write_text(stubs([s.read_text(encoding='utf-8') for s in sources]), encoding='utf-8')
        tables = ''.join(f'    table("{member}", &MachineRecord::{member}, {pointer}, {count});\n'
                         for member, pointer, count in TABLES)
        (folder / 'check.cpp').write_text(CHECK.replace('@TABLES@', tables).replace('@COUNT@', str(len(machines) + 1)),
                                          encoding='utf-8')
        binary = folder / ('check.exe' if os.name == 'nt' else 'check')
        command = [HOST_CXX, '-std=c++11', '-O1', '-Wall', '-Werror', '-Wno-unused-function',
                   '-D__builtin_bfin_ssync()=__sync_synchronize()',
                   '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                   *map(str, sources), str(folder / 'records.cpp'), str(folder / 'stubs.cpp'), str(folder / 'check.cpp'),
                   str(PLATFORM / 'machine_tables.cpp'), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, **HIDDEN)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True, **HIDDEN)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return dict(line.split() for line in result.stdout.split('\n') if line)

    def test_startup_copies_every_record_row_into_its_table(self):
        machines = every_row()
        with tempfile.TemporaryDirectory(prefix='records-') as folder:
            counts = self.build(machines, folder)
        # Every table holds rows, so each copy is exercised.
        self.assertTrue(all(int(n) > 0 for n in counts.values()), counts)
        self.assertEqual(int(counts['companion']), len(machines) - 1)  # all but the player

    def test_one_machine_and_a_selection_of_each_kind(self):
        for machines in ([fixture('probefx')], [fixture('probepads')], [fixture('probekit25'), fixture('probesynth')]):
            with self.subTest(machines=[m.id for m in machines]), tempfile.TemporaryDirectory(prefix='records-') as folder:
                self.build(machines, folder)


class RecordOrder(unittest.TestCase):
    def test_records_list_by_kind_then_id(self):
        ordered = record_order(every_row())
        self.assertEqual([m.id for m in ordered], ['probeaux', 'probestage', 'probesynth', 'probekit23', 'probekit25',
                                                   'probemix', 'probesample', 'probecurves', 'probefx', 'probepads'])
        text = records.assembly(every_row())
        self.assertEqual(re.findall(r'\.long ([a-z0-9_]+_record)', text), [records.symbol(m) for m in ordered])
        self.assertIn(f'.space {records.space_bytes(10)}', text)

    def test_a_record_never_depends_on_the_other_machines(self):
        alone = {m.id: records.render(m) for m in every_row()}
        for m in every_row():
            self.assertEqual(records.render(m), alone[m.id])
            self.assertNotIn('probe', alone[m.id].replace(m.id, '').replace(m.prefix, ''), m.id)

    def test_a_synth_or_kit_without_its_own_page_has_no_record(self):
        m = dataclasses.replace(fixture('probesynth'), display='stock')
        with self.assertRaisesRegex(ValueError, 'no page of its own'):
            records.render(m)


@unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
class RecordsObject(unittest.TestCase):
    def test_the_browser_writes_the_assemblers_object(self):
        """builder/core.mjs recordsObject is the pinned assembler's object for records.assembly, byte for byte,
        for selections of one, several and every kind."""
        assembler = blackfin_compiler().parent / ('bfin-elf-as' + blackfin_compiler().suffix)
        # Prefixes ending others, whose record names the assembler writes as one.
        ends = [SimpleNamespace(id=i, prefix=i, kind=k) for i, k in
                (('a', 'synth'), ('xa', 'synth'), ('ba', 'effect'), ('yxa', 'sample'), ('record', 'player'), ('b', 'drum'))]
        selections = [[fixture('probefx')], every_row(), every_row()[:3], ends, ends[:2], ends[1:4]]
        with tempfile.TemporaryDirectory(prefix='records-object-') as folder:
            folder = Path(folder)
            listed = []
            for i, machines in enumerate(selections):
                source, obj = folder / f'{i}.s', folder / f'{i}.o'
                source.write_text(records.assembly(machines), encoding='utf-8', newline='\n')
                subprocess.run([str(assembler), '-mcpu=bf707', '-o', str(obj), str(source)], check=True, **HIDDEN)
                listed.append(dict(path=obj.as_posix(), machines=[dict(id=m.id, prefix=m.prefix, kind=m.kind)
                                                                  for m in machines]))
            script = folder / 'compare.mjs'
            script.write_text('''import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
const core = await import(pathToFileURL(process.argv[2]).href);
for (const {path, machines} of JSON.parse(readFileSync(process.argv[3], 'utf8'))) {
  const a = core.recordsObject(machines), b = new Uint8Array(readFileSync(path));
  if (a.length !== b.length || a.some((v, i) => v !== b[i])) { console.log('differs: ' + path); process.exit(1); }
}
''', encoding='utf-8')
            (folder / 'selections.json').write_text(json.dumps(listed), encoding='utf-8')
            result = subprocess.run(['node', str(script), str(ROOT / 'builder/core.mjs'), str(folder / 'selections.json')],
                                    capture_output=True, text=True, **HIDDEN)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
