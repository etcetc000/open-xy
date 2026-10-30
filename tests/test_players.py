"""Player ABI and integration contracts; musical tests live with each player."""
from dataclasses import replace
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import shutil
import unittest

from tooling import adapters, native
from tooling.formats import ldr, mapped_offset
from tooling.machines import machine_at, assign_numbers, number_layout
from tooling.pipeline import config
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


def vtable():
    source = (PLATFORM / 'player_object.cpp.in').read_text(encoding='utf-8')
    start = source.index('_player_vtable[16] = {')
    body = source[start:source.index('};', start)].split('{', 1)[1]
    return [entry.strip() for entry in body.split(',') if entry.strip()]


class PlayerContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'machine.json').write_text(json.dumps(dict(schema=1, sdk='1.7', kind='player',
                                                               description='Player contract fixture.')))
        definition = dict(schema=1, id='probeplayer', name='Player', resources={}, presets={},
                          parameters=[dict(id='p'+str(i), label='P'+str(i), default=0,
                                           mapping=dict(kind='linear', minimum=0, maximum=1), display='percent')
                                      for i in range(4)])
        (self.root / 'definition.json').write_text(json.dumps(definition))
        self.machine = assign_numbers([machine_at(self.root)])[0][0]

    def test_layout_keeps_player_types_and_shares_page_pool(self):
        m = self.machine
        rules = config()['identities']
        self.assertIn(m.identity['player'], rules['players'])
        requested = dict(player=None, page=None)
        layout = {m.id: m.identity}
        numbered = number_layout(rules, [], layout, [(m.id, 'player', requested), ('other', 'player', requested)])
        self.assertEqual(numbered[m.id], m.identity)
        self.assertNotEqual(numbered['other']['player'], m.identity['player'])
        self.assertNotEqual(numbered['other']['page'], m.identity['page'])
        for invalid in (0, 1, 2, 3, 4, 5, 32):
            with self.assertRaises(ValueError):
                number_layout(rules, [], {m.id: dict(player=invalid, page=90)}, [(m.id, 'player', requested)])

    def test_adapter_is_independent_of_numbers_and_binds_output_keyboard(self):
        m = self.machine
        other = replace(m, identity=dict(player=31, page=255))
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            first = adapters.generate_player_object(a, m).read_text(encoding='utf-8')
            self.assertEqual(first, adapters.generate_player_object(b, other).read_text(encoding='utf-8'))
        self.assertNotIn('@', first)
        self.assertIn('probeplayer_key_127', first)
        self.assertIn('probeplayer_played_127', first)
        self.assertIn('player_output(object, shown)', first)
        self.assertEqual(len(vtable()), 16)
        for i, entry in enumerate(vtable()):
            self.assertEqual('0x' not in entry, i in {2, 5, 6})

    def test_catalog_player_is_not_an_instrument(self):
        profile = config()
        catalog = (native.STOCK_CONTENT / 'op1_factory.json').read_bytes()
        before = json.loads(catalog)
        after = json.loads(native.extend_catalog(catalog, profile, [self.machine]))
        self.assertEqual(after['engine_types'], before['engine_types'])
        self.assertEqual(after['fx_types'], before['fx_types'])
        self.assertEqual(after['seq_types'][-1]['id'], self.machine.identity['player'] + 4)
        self.assertEqual(after['seq_types'][-1]['type'], 'probeplayer')

    def test_player_code_and_octave_hook_are_selection_gated(self):
        m = self.machine
        from tooling.firmware import active_services
        profile = config()
        self.assertEqual(active_services(profile, ['player']), {'players', 'octaves'})
        self.assertEqual(active_services(profile, ['synth']), set())
        self.assertEqual(native.absent_symbols(m), {'probeplayer_player': 'kit_absent',
                                                   'probeplayer_screen_register': 'kit_absent'})
        self.assertEqual([step for _, machine, step in native.plan([m]) if machine], ['player_object'])

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_activity_and_keyboard_range(self):
        binary = self.root / ('activity.exe' if os.name == 'nt' else 'activity')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(ROOT/'sdk/include'),
                        '-I'+str(PLATFORM), str(ROOT/'tests/fixtures/player_activity.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120, **flags)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30, **flags)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_real_renderer_keeps_header_and_fits_output_notes(self):
        shutil.copy(PLATFORM / 'screen_player.hpp', self.root)
        shutil.copy(ROOT / 'tests/fixtures/screen_player_host.hpp', self.root / 'screen.hpp')
        binary = self.root / ('screen.exe' if os.name == 'nt' else 'screen')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(self.root),
                        '-I'+str(ROOT/'sdk/include'), '-I'+str(PLATFORM),
                        str(ROOT/'tests/fixtures/screen_player.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120, **flags)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30, **flags)


@unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS not supplied')
class StockPlayer(unittest.TestCase):
    def test_chord_storage_matches_stock_step_and_release(self):
        from tooling.formats import sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        records, held, limit = (int(value, 0) for value in re.search(
            r'kRecords = (0x[0-9a-f]+), kHeld = (0x[0-9a-f]+), kMaxHeld = (\d+)',
            (PLATFORM / 'players.cpp').read_text(encoding='utf-8')).groups())
        self.assertEqual(held - records, limit * 72)

        def instruction(address, opcode, immediate):
            at = mapped_offset(ldr(data), address, 4)
            self.assertEqual(data[at:at+4], struct.pack('<HH', opcode, immediate),
                             f'Player chord layout disagrees with stock at {address:#x}')

        # Decode the SDK's layout against both stock consumers, not against
        # another copy of the assumed offsets. OS 1.2.1 moved these fields.
        instruction(0x8d57d8d4, 0xe121, records)  # step: R1 = record-array offset
        instruction(0x8d57d8e0, 0xe428, held // 4)  # step: R0 = [P5 + held]
        instruction(0x8d57d8dc, 0xe125, 72)  # step: chord stride
        instruction(0x8d57d74c, 0xe122, records)  # release: R2 = record-array offset
        instruction(0x8d57d758, 0xe428, held // 4)  # release: R0 = [P5 + held]
        instruction(0x8d57d754, 0xe124, 72)  # release: chord stride

    def test_kept_methods_are_authenticated_stock_maestro(self):
        from tooling.formats import sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        at = mapped_offset(ldr(data), 0x8409f6d0, 64)
        words = struct.unpack('<16I', data[at:at+64])
        for i, entry in enumerate(vtable()):
            if i not in {2, 5, 6}:
                self.assertEqual(int(entry.split('(')[1].rstrip(')'), 16), words[i])
