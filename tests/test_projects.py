"""Project companions: what each saved machine number meant, by machine ID."""
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.machines import LAYOUT_FORMAT
from tooling import native
from tooling.adapters import generate_project_companion_table
from tooling.pipeline import ROOT, config
from machine_fixtures import NAMES, fixture
from tooling.projects import (LIMIT, MACHINES, canonical, companion, companion_path, main, parse, saved_numbers, translation)

PROJECT = bytes(range(256)) * 4
SAVED = {'karplus': {'engine': 36, 'page': 84}, 'pingpong': {'effect': 40, 'page': 21},
         'timestretch': {'engine': 35, 'page': 7}}


def edited(document, **changes):
    return json.dumps(dict(document, **changes)).encode()


class Companions(unittest.TestCase):
    def test_round_trip_gives_the_saving_layout(self):
        document = parse(companion(PROJECT, SAVED))
        self.assertEqual(saved_numbers(document, PROJECT), SAVED)
        self.assertEqual(document['sections'][MACHINES]['profile'], config()['id'])

    def test_writers_emit_one_canonical_form(self):
        raw = companion(PROJECT, SAVED)
        self.assertEqual(raw, companion(PROJECT, dict(reversed(SAVED.items()))))
        self.assertEqual(raw, canonical(json.loads(raw)))
        self.assertTrue(raw.endswith(b'}\n') and b'\r' not in raw)
        self.assertGreater(raw.count(b'\n'), 20, 'one value per line')

    def test_project_saved_since_is_read_as_saved(self):
        # Stock, or firmware without companions, rewrote the project after the companion.
        document = parse(companion(PROJECT, SAVED))
        self.assertIsNone(saved_numbers(document, PROJECT + b'\0'))
        self.assertIsNone(saved_numbers(document, bytes(reversed(PROJECT))))

    def test_other_firmware_or_unknown_section_versions_are_ignored(self):
        document = parse(companion(PROJECT, SAVED, profile='opxy-9.9.9'))
        self.assertIsNone(saved_numbers(document, PROJECT))
        document = json.loads(companion(PROJECT, SAVED))
        document['sections'][MACHINES] = {'version': 2, 'anything': True}
        self.assertIsNone(saved_numbers(parse(json.dumps(document).encode()), PROJECT))

    def test_unknown_keys_and_sections_are_ignored_and_kept_while_they_apply(self):
        document = json.loads(companion(PROJECT, SAVED))
        document['written'] = 'later field'
        document['sections']['someone.tuning'] = {'version': 3, 'scale': [0, 2, 4]}
        previous = parse(json.dumps(document).encode())
        self.assertEqual(saved_numbers(previous, PROJECT), SAVED)
        kept = parse(companion(PROJECT, {'karplus': {'engine': 37, 'page': 85}}, previous))
        self.assertEqual(kept['sections']['someone.tuning'], {'version': 3, 'scale': [0, 2, 4]})
        dropped = parse(companion(PROJECT + b'!', SAVED, previous))
        self.assertEqual(set(dropped['sections']), {MACHINES})

    def test_invalid_companions_are_refused_whole(self):
        document = json.loads(companion(PROJECT, SAVED))
        for raw in (b'not json', b'[]', edited(document, format='other'), edited(document, version=2),
                    edited(document, version=0), edited(document, project={'bytes': 1, 'crc32': 'XYZ'}),
                    edited(document, sections={'nodot': {'version': 1}}),
                    edited(document, sections={'a.b': {'version': '1'}}),
                    edited(document, sections={MACHINES: {'version': 1, 'profile': 'x', 'machines': {'k': {'engine': 1}}}}),
                    b' ' * (LIMIT + 1)):
            with self.subTest(raw=raw[:40]), self.assertRaises(ValueError):
                parse(raw)

    def test_translation_follows_machine_ids(self):
        here = {'karplus': {'engine': 41, 'page': 90}, 'pingpong': {'effect': 40, 'page': 21}}
        self.assertEqual(translation(SAVED, here), {
            ('engine', 36): 41, ('page', 84): 90,   # renumbered in this layout
            ('effect', 40): 40,                     # same number
            ('engine', 35): None, ('page', 7): None,  # not installed here
        })

    def test_command_writes_and_checks_beside_each_project(self):
        with tempfile.TemporaryDirectory() as directory:
            project, layout = Path(directory) / 'song.xy', Path(directory) / 'layout.json'
            project.write_bytes(PROJECT)
            layout.write_text(json.dumps({'format': LAYOUT_FORMAT, 'profile': config()['id'], 'machines': SAVED}))
            self.assertEqual(main(['write', '--layout', str(layout), str(project)]), 0)
            self.assertEqual(companion_path(project), Path(directory) / 'song.oxy')
            self.assertEqual(saved_numbers(parse(companion_path(project).read_bytes()), PROJECT), SAVED)
            self.assertEqual(main(['check', str(project)]), 0)



def host_writer(folder):
    """The real firmware writer built for the host, its stock file calls on host stdio."""
    from test_sample_services import HOST_CXX
    platform = ROOT / 'sdk/platform/opxy-1.2.1'
    stock = (platform / 'stock.hpp').read_text(encoding='utf-8')
    stock = stock.replace('typedef __UINTPTR_TYPE__ Address;', 'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
    stock = stock.replace('reinterpret_cast<T*>(address)', 'reinterpret_cast<T*>(host_stock_address(address))')
    (folder / 'stock.hpp').write_text(stock, encoding='utf-8')
    (folder / 'project_companion.cpp').write_bytes((platform / 'project_companion.cpp').read_bytes())
    binary = folder / ('writer.exe' if os.name == 'nt' else 'writer')
    result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                             '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                             str(ROOT / 'tests/fixtures/project_companion.cpp'), '-o', str(binary)],
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return binary


class FirmwareWriter(unittest.TestCase):
    def setUp(self):
        from test_sample_services import HOST_CXX
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')

    def test_writes_the_companion_the_reference_writes_after_a_save(self):
        with tempfile.TemporaryDirectory(prefix='companion-') as directory:
            folder = Path(directory)
            writer, project = host_writer(folder), folder / 'song.xy'
            self.assertEqual(subprocess.run([str(writer), str(project), '1'], timeout=30).returncode, 0)
            # The fixture's table: what its numbers mean, a kit machine left out omitted.
            self.assertEqual((folder / 'song.oxy').read_bytes(), companion(project.read_bytes(), SAVED))

    def test_a_failed_save_or_other_name_writes_nothing(self):
        with tempfile.TemporaryDirectory(prefix='companion-') as directory:
            folder = Path(directory)
            writer = host_writer(folder)
            self.assertEqual(subprocess.run([str(writer), str(folder / 'song.xy'), '0'], timeout=30).returncode, 0)
            self.assertEqual(subprocess.run([str(writer), str(folder / 'song.bin'), '1'], timeout=30).returncode, 0)
            self.assertEqual(sorted(p.name for p in folder.glob('song*')), ['song.bin'])

    def test_every_save_reaches_the_writer_through_one_reviewed_call(self):
        hooks = [p for p in config()['patches'] if p.get('symbol') == 'sdk_project_save']
        self.assertEqual([(p['address'], p['before'], p['call']) for p in hooks], [('0x8d585eda', '00e3cd98', True)])
        self.assertIn('project_companion', native.SDK_UNITS)
        self.assertIn('project_companion_table', native.SDK_UNITS)

    def test_mtp_shows_companions_beside_projects(self):
        import re
        edits = [p for p in config()['patches'] if p['kind'] == 'text']
        self.assertEqual(len(edits), 3)
        for edit in edits:
            before = bytes.fromhex(edit['before']).rstrip(b'\0').decode()
            after = bytes.fromhex(edit['after']).rstrip(b'\0').decode()
            for name in ('/projects/workspace', '/projects/a b/song', '/projects/x/y 2/song'):
                for suffix, stock, ours in (('.xy', True, True), ('.oxy', False, True), ('.txt', False, False)):
                    if re.fullmatch(before, name + '.xy'):
                        self.assertEqual(bool(re.fullmatch(before, name + suffix)), stock)
                        self.assertEqual(bool(re.fullmatch(after, name + suffix)), ours, (after, name + suffix))

    def test_table_lists_each_machine_by_id_with_its_number_symbols(self):
        with tempfile.TemporaryDirectory() as directory:
            machines = [fixture(name) for name in NAMES]
            source = generate_project_companion_table(directory, machines).read_text()
        ids = re.findall(r'\{"([a-z0-9]+)", ', source)
        # Step components have no numbers for a companion to record.
        self.assertEqual(ids, sorted(m.id for m in machines if m.kind != 'step'))
        self.assertIn('{"probefx", probefx_fx_id, probefx_fx_page_id, true},', source)


if __name__ == '__main__':
    unittest.main()
