"""Note players: generated adapters, catalog entries, identities and each player's own vectors."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from tooling import adapters
from tooling.formats import ldr, mapped_offset
from tooling.pipeline import read
from tooling.machines import check_identity, discovered, machine
from tooling.native import extend_catalog, validate_panel
from tooling.startup import render
from tooling.pipeline import config
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
MAESTRO_VTABLE = 0x8409c3d8
REPLACED = {2, 5, 6}  # type, note-on, note-off


def players():
    return [m for m in discovered() if m.kind == 'player']


def template_vtable():
    text = (ROOT / 'sdk/platform/opxy-1.2.0/player_object.cpp.in').read_text(encoding='utf-8')
    body = text[text.index('_player_vtable[16] = {'):text.index('};', text.index('_player_vtable[16] = {'))]
    entries = [part.strip() for part in body.split('{', 1)[1].split(',') if part.strip()]
    return entries


class Generation(unittest.TestCase):
    def test_object_adapter_keeps_maestro_except_three_methods(self):
        entries = template_vtable()
        self.assertEqual(len(entries), 16)
        for i, entry in enumerate(entries):
            self.assertEqual(i in REPLACED, '0x' not in entry, entry)
        m = machine('chords')
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_object(directory, m).read_text(encoding='utf-8')
        self.assertIn('static u32 type(void*) { return %d; }' % m.identity['player'], text)
        self.assertIn('"chords"', text)
        self.assertNotIn('@', text)

    def test_table_lists_only_players(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_table(directory, discovered()).read_text(encoding='utf-8')
        for m in discovered():
            self.assertEqual(m.prefix + '_player()' in text, m.kind == 'player')

    def test_catalog_entry_joins_seq_types_with_stock_numbering(self):
        catalog = (b'{\n\t"engine_types": [\n        {\n            "type": "a",\n            "default_params": [0]\n        }\n    ],\n'
                   b'\t"seq_types": [\n        {\n            "type": "maestro",\n            "id": 8,\n'
                   b'            "default_params": [0, 0, 0, 0, 0, 0, 0, 0]\n        }\n    ],\n\t"x": 1\n}\n')
        ends = {key: catalog.index(b'\n    ]', catalog.index(key)) for key in (b'"engine_types"', b'"seq_types"')}
        profile = dict(stock_catalog_inserts={'engine_types': ends[b'"engine_types"'], 'seq_types': ends[b'"seq_types"']})
        m = machine('chords')
        parsed = json.loads(extend_catalog(catalog, profile, [m]))
        entry = parsed['seq_types'][-1]
        self.assertEqual((entry['type'], entry['id']), ('chords', m.identity['player'] + 4))
        self.assertEqual(entry['default_params'], adapters.catalog_defaults(m.definition))
        with self.assertRaisesRegex(ValueError, 'catalog list'):
            extend_catalog(catalog, dict(stock_catalog_inserts={'engine_types': ends[b'"engine_types"']}), [m])


class Identity(unittest.TestCase):
    def test_player_types_avoid_stock_values(self):
        rules = config()['identities']
        self.assertEqual(check_identity(rules, 'player', {'player': 6}), {'player': 6})
        self.assertEqual(check_identity(rules, 'player', {'player': 6, 'page': 84}), {'player': 6, 'page': 84})
        for value in ({'player': 4}, {'player': 5}, {'player': 32}, {'player': 6, 'page': 46}, {'player': 6, 'page': 81},
                      {'player': 6, 'page': 128}, {'engine': 6}, {'player': 6, 'page': 84, 'engine': 1}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                check_identity(rules, 'player', value)


class Page(unittest.TestCase):
    def test_page_unit_registers_and_binds_the_artwork(self):
        m = machine('chords')
        self.assertEqual(m.display, 'player')
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_object(directory, m).read_text(encoding='utf-8')
        self.assertNotIn('@', text)
        self.assertIn('int chords_screen_register()', text)
        self.assertIn('"chords_dot_23",', text)
        self.assertIn('"chords_text",', text)
        self.assertIn('{"str", "dir", "voi", "spr"}', text)
        self.assertIn('chords_player_vtable, chords_page_id, chords_screen_ready}', text)

    def test_startup_registers_player_pages_on_the_gui_role(self):
        source = render(['synth'], [], [], ['chords'])
        screen = source[source.index('void sdk_startup_screen()'):source.index('// The stock catalog')]
        self.assertIn('chords_screen_register();', screen)
        with self.assertRaises(ValueError):
            render(['chords'], [], [], ['chords'])

    def test_artwork_needs_every_dot_and_the_line(self):
        m = machine('chords')
        fragment = (m.root / 'panel.svg.fragment').read_bytes()
        validate_panel(m, fragment)
        for broken in (fragment.replace(b'id="chords_dot_23"', b'id="chords_dot_x"'),
                       fragment.replace(b'id="chords_text"', b'id="chords_line"'),
                       fragment.replace(b'<circle id="chords_dot_0" transform="matrix(1 0 0 1', b'<circle id="chords_dot_0" transform="matrix(2 0 0 1')):
            with self.subTest(), self.assertRaises(ValueError):
                validate_panel(m, broken)


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class Vectors(unittest.TestCase):
    def test_each_player_passes_its_own_vectors(self):
        for m in players():
            for source in sorted((m.root / 'tests').glob('*.cpp')):
                with self.subTest(player=m.id, source=source.name), tempfile.TemporaryDirectory() as folder:
                    binary = Path(folder) / ('vectors.exe' if os.name == 'nt' else 'vectors')
                    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
                    command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                               str(source), '-o', str(binary)]
                    result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


@unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS not supplied')
class Stock(unittest.TestCase):
    def test_kept_methods_are_stock_maestro(self):
        stock = read(Path(os.environ['OPXY_TEST_STOCK_OS']))
        blocks = ldr(stock)
        at = mapped_offset(blocks, MAESTRO_VTABLE, 64)
        words = struct.unpack('<16I', stock[at:at + 64])
        for i, entry in enumerate(template_vtable()):
            if i not in REPLACED:
                self.assertEqual(int(entry.split('(')[1].rstrip(')'), 16), words[i], i)


if __name__ == '__main__':
    unittest.main()
