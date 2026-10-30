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
from tooling.projects import (FALLBACK, LIMIT, MACHINE_LIMIT, MACHINES, binding, canonical, companion, companion_path, main, parse,
                              saved_numbers, translation)

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



def host_writer(folder, fixture='project_companion.cpp'):
    """The real firmware writer and reader built for the host, its stock file calls on host stdio."""
    from test_sample_services import HOST_CXX
    platform = ROOT / 'sdk/platform/opxy-1.2.1'
    stock = (platform / 'stock.hpp').read_text(encoding='utf-8')
    stock = stock.replace('typedef __UINTPTR_TYPE__ Address;', 'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
    stock = stock.replace('reinterpret_cast<T*>(address)', 'reinterpret_cast<T*>(host_stock_address(address))')
    (folder / 'stock.hpp').write_text(stock, encoding='utf-8')
    (folder / 'project_companion.cpp').write_bytes((platform / 'project_companion.cpp').read_bytes())
    binary = folder / (Path(fixture).stem + ('.exe' if os.name == 'nt' else ''))
    result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                             '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                             '-I' + str(ROOT / 'tests/fixtures'),
                             str(ROOT / 'tests/fixtures' / fixture), '-o', str(binary)],
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

    def test_a_companion_larger_than_the_write_buffer_is_written_whole(self):
        # 220 machines numbered past the stock imm7 bound: the writer streams through its
        # 8 KiB buffer, and a reader takes every machine.
        layout = {f'm{i:03d}': {'engine': 34 + i, 'page': 100} for i in range(220)}
        table = ''.join(f'    {{"{name}", number({n["engine"]}), number({n["page"]}), false}},\n'
                        for name, n in layout.items())
        source = (ROOT / 'tests/fixtures/project_companion.cpp').read_text(encoding='utf-8')
        start = source.index('const opxy::platform::CompanionMachine sdk_companion_machines[] = {\n')
        end = source.index('    {nullptr, nullptr, nullptr, false}', start)
        with tempfile.TemporaryDirectory(prefix='companion-') as directory:
            folder = Path(directory)
            many = folder / 'project_companion_many.cpp'
            many.write_text(source[:start] + 'const opxy::platform::CompanionMachine sdk_companion_machines[] = {\n' +
                            table + source[end:], encoding='utf-8')
            writer, project = host_writer(folder, many), folder / 'song.xy'
            self.assertEqual(subprocess.run([str(writer), str(project), '1'], timeout=30).returncode, 0)
            written = (folder / 'song.oxy').read_bytes()
            self.assertGreater(len(written), 8 * 1024)  # several writer buffers
            self.assertEqual(written, companion(project.read_bytes(), layout))

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
        self.assertEqual(ids, sorted(m.id for m in machines))
        self.assertIn('{"probefx", probefx_fx_id, probefx_fx_page_id, true},', source)


# The reader fixture's firmware (tests/fixtures/project_load.cpp): what each machine is here.
HERE = {'fm2op': {'engine': 35, 'page': 60}, 'karplus': {'engine': 41, 'page': 90},
        'macroosc': {'engine': 36, 'page': 61}, 'pingpong': {'effect': 40, 'page': 21},
        'ringmod': {'effect': 36, 'page': 21}}
# Saved patterns by track: instrument tracks 0-7 hold engines, sends 14 and 15 effects,
# tracks 8-13 no machine. 36 is karplus's old engine and ringmod's effect here.
TRACKS = {0: [36, 35, 18, 41, 50], 7: [36], 9: [36, 35], 14: [40, 36, 5], 15: [36]}
PRISM, NO_EFFECT = FALLBACK['engine'], FALLBACK['effect']


def reference(raw, data, tracks):
    """What tooling/projects.py makes of the project's numbers: a missing machine, or a
    number two listed machines claim, becomes stock's fallback for the track."""
    try:
        saved = saved_numbers(parse(raw), data)
    except ValueError:
        saved = None
    if saved is None:
        return tracks
    table = translation(saved, HERE)
    slot = lambda track: 'engine' if track < 8 else 'effect'
    return {t: [n if 8 <= t < 14 or (slot(t), n) not in table else
                FALLBACK[slot(t)] if table[(slot(t), n)] is None else table[(slot(t), n)] for n in numbers]
            for t, numbers in tracks.items()}


def machines_section(machines, profile=None):
    """An open-xy.machines section's text: `machines` as (ID, value text) pairs, kept in
    order and repeated as given, which json.dumps cannot write."""
    listed = ', '.join(f'"{name}": {value}' for name, value in machines)
    return '{"version": 1, "profile": "%s", "machines": {%s}}' % (profile or config()['id'], listed)


def raw_companion(machines, sections=None, data=PROJECT):
    """A companion for `data` whose machines section lists `machines`, then `sections`
    as (name, value text) pairs."""
    members = [(MACHINES, machines_section(machines))] + list(sections or ())
    listed = ', '.join(f'"{name}": {value}' for name, value in members)
    return ('{"format": "open-xy-project", "version": 1, "project": %s, "sections": {%s}}' %
            (json.dumps(binding(data)), listed)).encode()


class FirmwareReader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_sample_services import HOST_CXX
        if not HOST_CXX:
            raise unittest.SkipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        cls.folder = tempfile.TemporaryDirectory(prefix='companion-reader-')
        cls.reader = host_writer(Path(cls.folder.name), 'project_load.cpp')

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def load(self, raw, data=PROJECT, tracks=TRACKS, result=1, project=PROJECT):
        """The tracks after the firmware loads `project` beside companion bytes `raw`."""
        with tempfile.TemporaryDirectory(prefix='companion-') as directory:
            song = Path(directory) / 'song.xy'
            song.write_bytes(project)
            if raw is not None:
                (Path(directory) / 'song.oxy').write_bytes(raw)
            specs = [f"{t}:{','.join(map(str, n))}" for t, n in tracks.items()]
            run = subprocess.run([str(self.reader), str(song), str(result), *specs], capture_output=True, text=True,
                                 timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr)
        return {int(t): [int(n) for n in numbers.split(',')]
                for t, numbers in (line.split(':') for line in run.stdout.split())}

    def check(self, raw, expected, **kwargs):
        loaded = self.load(raw, **kwargs)
        self.assertEqual(loaded, expected)
        self.assertEqual(loaded, reference(raw, PROJECT, kwargs.get('tracks', TRACKS)), 'tooling/projects.py agrees')

    def test_maps_listed_numbers_by_machine_id_per_track_kind(self):
        self.check(companion(PROJECT, SAVED), {
            0: [41, PRISM, 18, 41, 50],       # karplus renumbered; timestretch missing, its 35 now fm2op's
            7: [41],                            # every instrument track
            9: [36, 35],                        # tracks 8-13 hold no machine
            14: [40, 36, 5],                    # pingpong kept; effect 36 is not karplus's engine 36
            15: [36]})

    def test_numbers_trade_places_at_once(self):
        saved = {'fm2op': {'engine': 41, 'page': 60}, 'karplus': {'engine': 35, 'page': 90},
                 'pingpong': {'effect': 36, 'page': 21}, 'ringmod': {'effect': 40, 'page': 21}}
        self.check(companion(PROJECT, saved), {0: [35, 41], 14: [36, 40]}, tracks={0: [41, 35], 14: [40, 36]})

    def test_missing_machines_fall_back_and_never_open_another(self):
        saved = {'gone': {'engine': 50, 'page': 99},       # free here
                 'macroosc': {'engine': 37, 'page': 61},
                 'skipped': {'engine': 38, 'page': 98},    # in this kit but left out
                 'pingpong': {'engine': 39, 'page': 21},   # an effect here
                 'ringmod': {'effect': 40, 'page': 21}}    # 40 is pingpong's here
        self.check(companion(PROJECT, saved), {0: [PRISM, 36, PRISM, PRISM], 14: [36]},
                   tracks={0: [50, 37, 38, 39], 14: [40]})

    def test_two_machines_at_one_number_trust_neither(self):
        raw = companion(PROJECT, SAVED).replace(b'"engine": 35', b'"engine": 36')
        self.check(raw, {0: [PRISM, 35], 14: [40]}, tracks={0: [36, 35], 14: [40]})
        # Engines and effects alike, whether or not each machine is here.
        saved = {'karplus': {'engine': 36, 'page': 84}, 'twin': {'engine': 36, 'page': 85},
                 'pingpong': {'effect': 40, 'page': 21}, 'ringmod': {'effect': 40, 'page': 21}}
        self.check(companion(PROJECT, saved), {0: [PRISM], 14: [NO_EFFECT]}, tracks={0: [36], 14: [40]})

    def test_a_repeated_key_counts_once_its_last_value_deciding(self):
        engine = lambda n, page=84: '{"engine": %s, "page": %s}' % (n, page)
        cases = {
            'a machine listed again': ([('karplus', engine(36)), ('karplus', engine(37))], None),
            'an invalid entry replaced': ([('karplus', '5'), ('karplus', engine(37))], None),
            'a valid entry replaced by an invalid one': ([('karplus', engine(37)), ('karplus', engine(1.5))], None),
            'a field given twice': ([('karplus', '{"engine": 1.5, "engine": 37, "page": 84}')], None),
            'a claim withdrawn': ([('karplus', engine(36)), ('twin', engine(36)), ('twin', engine(37))], None),
            'a claim added': ([('karplus', engine(37)), ('twin', engine(38)), ('twin', engine(37))], None),
            'an invalid section replaced': ([('karplus', engine(37))], [('a.b', '{}'), ('a.b', '{"version": 1}')]),
            'a valid section replaced by an invalid one': ([('karplus', engine(37))],
                                                         [('a.b', '{"version": 1}'), ('a.b', '[]')]),
            'the machines section given twice': ([('karplus', engine(36))],
                                                 [(MACHINES, machines_section([('karplus', engine(37))]))]),
        }
        tracks = {0: [36, 37, 38]}
        expected = {'a machine listed again': {0: [36, 41, 38]},
                    'an invalid entry replaced': {0: [36, 41, 38]},
                    'a valid entry replaced by an invalid one': tracks,
                    'a field given twice': {0: [36, 41, 38]},
                    'a claim withdrawn': {0: [41, PRISM, 38]},
                    'a claim added': {0: [36, PRISM, 38]},
                    'an invalid section replaced': {0: [36, 41, 38]},
                    'a valid section replaced by an invalid one': tracks,
                    'the machines section given twice': {0: [36, 41, 38]}}
        for name, (machines, sections) in cases.items():
            with self.subTest(name):
                self.check(raw_companion(machines, sections), expected[name], tracks=tracks)
        # Only the last sections object counts, and the last binding.
        raw = raw_companion([('karplus', engine(37))]).decode()
        self.check(('{"sections": {}, ' + raw[1:]).encode(), {0: [36, 41, 38]}, tracks=tracks)
        self.check((raw[:-1] + ', "sections": {}}').encode(), tracks, tracks=tracks)
        self.check((raw[:-1] + ', "project": {"bytes": 1, "crc32": "00000000"}}').encode(), tracks, tracks=tracks)

    def test_both_readers_use_one_machine_id_rule_and_limit(self):
        tracks = {0: [36, 50]}
        listed = lambda *names: [(name, '{"engine": 50, "page": 99}') for name in names]
        for names, expected in (((), {0: [36, 50]}),
                                (('int', 'prism', 'sdk_x', 'a', 'a_2345678901234'), {0: [36, PRISM]}),
                                (('Karplus',), tracks), (('a23456789012345x',), tracks), (('_a',), tracks),
                                (('caf\\u00e9',), tracks), (('9lives',), tracks), (('',), tracks)):
            with self.subTest(names=names):
                self.check(raw_companion(listed(*names)), expected, tracks=tracks)
        many = [(f'm{i}', '{"engine": 99, "page": 99}') for i in range(MACHINE_LIMIT - 1)]
        karplus = ('karplus', '{"engine": 36, "page": 84}')
        self.check(raw_companion(many + [karplus, ('m0', '{"engine": 98, "page": 1}')]), {0: [41, 50]}, tracks=tracks)
        self.check(raw_companion(many + [karplus, ('m', '{"engine": 99, "page": 1}')]), tracks, tracks=tracks)

    def test_numbers_beyond_the_records_are_never_mapped(self):
        tracks = {0: [36, 0, 62]}
        machines = [('karplus', '{"engine": -36, "page": 84}'), ('fm2op', '{"engine": -0, "page": 60}'),
                    ('macroosc', '{"engine": 256, "page": 61}'), ('gone', '{"engine": 62, "page": 61}')]
        self.check(raw_companion(machines), {0: [36, 35, PRISM]}, tracks=tracks)

    def test_numbers_above_the_stock_imm7_bound_are_mapped(self):
        # The relocated engine table runs to 256: a machine numbered past 63 there that
        # this firmware lacks falls back like any other.
        tracks = {0: [36, 200, 255]}
        machines = [('karplus', '{"engine": 36, "page": 84}'), ('high', '{"engine": 200, "page": 200}'),
                    ('top', '{"engine": 255, "page": 255}')]
        self.check(raw_companion(machines), {0: [41, PRISM, PRISM]}, tracks=tracks)

    def test_any_json_form_reads_the_same(self):
        document = json.loads(companion(PROJECT, SAVED))
        document['later'] = [1, {'nested': [True, None, -2.5e3]}]
        document['sections']['someone.tuning'] = {'version': 3, 'scale': [0, 2, 4], 'name': 'café \U0001d11e'}
        compact = json.dumps(dict(reversed(list(document.items()))), separators=(',', ':'), ensure_ascii=True)
        compact = compact.replace('open-xy-project', 'open-xy-pr\\u006fject')
        expected = self.load(companion(PROJECT, SAVED))
        # Lone surrogate halves, kept as Python keeps them, whatever escape follows.
        lone = compact[:-1] + r', "lone": "\ud800\n\udbff\ud800\udc00x\udc00\ud800\"\ud800"}'
        for raw in (compact.encode(), json.dumps(document, ensure_ascii=False, indent='\t').encode('utf-8'),
                    b'\r\n ' + compact.encode() + b' \n', lone.encode()):
            with self.subTest(raw=raw[:40]):
                self.check(raw, expected)

    def test_a_companion_that_does_not_apply_leaves_the_project_as_saved(self):
        document = json.loads(companion(PROJECT, SAVED))
        machines = document['sections'][MACHINES]
        cases = {
            'binding: project saved since': companion(PROJECT + b'!', SAVED),
            'binding: same size, other bytes': companion(bytes(reversed(PROJECT)), SAVED),
            'newer envelope': edited(document, version=2),
            'newer section': edited(document, sections={MACHINES: dict(machines, version=2)}),
            'other profile': companion(PROJECT, SAVED, profile='opxy-9.9.9'),
            'other format': edited(document, format='other'),
            'no binding': json.dumps({k: v for k, v in document.items() if k != 'project'}).encode(),
            'uppercase crc': edited(document, project={'bytes': len(PROJECT), 'crc32': document['project']['crc32'].upper()}),
            'float bytes': edited(document, project={'bytes': float(len(PROJECT)), 'crc32': document['project']['crc32']}),
            'bad section name': edited(document, sections=dict(document['sections'], Bad={'version': 1})),
            'section without version': edited(document, sections=dict(document['sections'], **{'a.b': {}})),
            'machine with a float': edited(document, sections={MACHINES: dict(machines, machines={'karplus': {'engine': 41.0, 'page': 90}})}),
            'machine without page': edited(document, sections={MACHINES: dict(machines, machines={'karplus': {'engine': 41}})}),
            'truncated': companion(PROJECT, SAVED)[:-3],
            'trailing data': companion(PROJECT, SAVED) + b'{}',
            'invalid utf-8': companion(PROJECT, SAVED).replace(b'"version": 1\n}', b'"version": 1, "x": "\xc0\xaf"\n}'),
            'over 64 KiB': companion(PROJECT, SAVED) + b' ' * LIMIT,
            'NaN': edited(document, later=float('nan')),
            'Infinity': edited(document, later=[float('-inf'), float('inf')]),
            'a lone high half before an invalid escape':
                companion(PROJECT, SAVED).replace(b'"version": 1\n}', b'"version": 1, "x": "\\ud800\\q"\n}'),
            'empty': b'',
        }
        for name, raw in cases.items():
            with self.subTest(name):
                self.check(raw, TRACKS)

    def test_exactly_64_kib_is_read(self):
        raw = companion(PROJECT, SAVED)
        raw += b' ' * (LIMIT - len(raw))
        self.check(raw, self.load(companion(PROJECT, SAVED)))

    def test_no_companion_or_a_failed_load_changes_nothing(self):
        self.assertEqual(self.load(None), TRACKS)
        self.assertEqual(self.load(companion(PROJECT, SAVED), result=0), TRACKS)

    def test_every_load_reaches_the_reader_through_one_reviewed_call(self):
        hooks = [p for p in config()['patches'] if p.get('symbol') == 'sdk_project_load']
        self.assertEqual([(p['address'], p['file_offset'], p['before'], p['call']) for p in hooks],
                         [('0x8d58913e', '0x1c039a', '00e3e984', True)])


if __name__ == '__main__':
    unittest.main()
