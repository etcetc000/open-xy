"""Machine numbers (engine, effect and page IDs): layouts give them, and they reach compiled
code only through link-time symbols, so one set of objects serves any numbering."""
from dataclasses import replace
import json
from pathlib import Path
import tempfile
import unittest

from tooling.adapters import (generate_display, generate_fx_object, generate_registration, generate_sample_callbacks,
                              generate_sample_voice, generate_screen, generate_synth_controls, generate_voice)
from tooling.machines import (FIXED_NUMBERS, LAYOUT_FORMAT, ROOT, by_kind, collection, layout_document, number_layout,
                              read_layout)
from tooling.pipeline import config
from tooling.startup import generate as generate_startup
from machine_fixtures import fixture, fixture_entries


def renumbered(m):
    """The same machine under other numbers. A sample engine's page is its stock page
    (sampler or drum), part of what it is, so only its engine changes."""
    identity = dict(m.identity)
    if m.kind == 'effect':
        identity['effect'] = 11 if identity['effect'] != 11 else 8
    else:
        identity['engine'] = 62 if identity['engine'] != 62 else 61
        if m.kind == 'synth':
            identity['page'] = 127 if identity['page'] != 127 else 126
    return replace(m, identity=identity)


def sources(directory, machines):
    """Every generated source of a collection, by name."""
    directory = Path(directory)
    for m in machines:
        folder = directory / m.id
        folder.mkdir()
        steps = {'synth': (generate_voice, generate_display, generate_screen, generate_registration),
                 'effect': (generate_registration, generate_fx_object),
                 'sample': (generate_registration, generate_sample_voice)}[m.kind]
        for step in steps:
            if step is generate_display:
                step(folder, machine=m)
            else:
                step(folder, m)
    shared = directory / 'sdk'
    shared.mkdir()
    generate_sample_callbacks(shared, machines)
    generate_synth_controls(shared, machines)
    generate_startup(shared, [m.prefix for m in by_kind(machines, 'synth', 'sample')],
                     [m.prefix for m in by_kind(machines, 'effect')], [m.prefix for m in by_kind(machines, 'sample')])
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in sorted(directory.rglob('*')) if p.is_file()}


class LinkTimeNumbers(unittest.TestCase):
    def test_generated_sources_do_not_depend_on_machine_numbers(self):
        machines = collection(fixture_entries())
        self.assertEqual({m.kind for m in machines}, {'synth', 'effect', 'sample'})
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            before, after = sources(first, machines), sources(second, [renumbered(m) for m in machines])
        self.assertEqual(sorted(before), sorted(after))
        for name in before:
            with self.subTest(source=name):
                self.assertEqual(before[name], after[name])


class Layouts(unittest.TestCase):
    def test_numbering_cases_shared_with_the_builder(self):
        cases = json.loads((ROOT / 'tests/fixtures/numbers.json').read_text(encoding='utf-8'))
        rules = config()['identities']
        fixed = cases['fixed']
        # The cases' fixed machines are the SDK's fixed numbers.
        self.assertEqual({name: identity for name, _kind, identity in fixed}, FIXED_NUMBERS)
        for case in cases['cases']:
            with self.subTest(case=case['name']):
                run = lambda: number_layout(rules, fixed, case.get('layout'), case['machines'])
                if 'error' in case:
                    with self.assertRaisesRegex(ValueError, case['error']):
                        run()
                else:
                    numbers = run()
                    self.assertEqual({k: v for k, v in numbers.items() if k not in {f[0] for f in fixed}},
                                     case['numbers'])

    def test_layout_files_round_trip_and_name_their_profile(self):
        numbers = FIXED_NUMBERS | {'pluck': {'engine': 36, 'page': 84}}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'layout.json'
            document = dict(layout_document(numbers), selected=['pluck'])  # the builder adds its selection
            path.write_text(json.dumps(document), encoding='utf-8')
            self.assertEqual(read_layout(path), numbers)
            for broken in (dict(document, format='opxy-kit/1'), dict(document, profile='opxy-9.9.9'),
                           dict(document, machines={'pluck': {'engine': '36', 'page': 84}})):
                with self.subTest(broken=broken):
                    path.write_text(json.dumps(broken), encoding='utf-8')
                    with self.assertRaises(ValueError):
                        read_layout(path)
        self.assertEqual(layout_document({})['format'], LAYOUT_FORMAT)

    def test_a_collection_takes_its_numbers_from_the_layout(self):
        entries = fixture_entries()
        requested = {m.id: m.identity for m in collection(entries)}
        self.assertEqual(requested, {e: fixture(e).identity for e in requested})
        # A layout's numbers win over the ones a machine asks for; dropped machines' stay held.
        synth = fixture('probesynth').identity
        layout = {'gone': {'engine': synth['engine'], 'page': 90}, 'probefx': {'effect': 11, 'page': 21}}
        machines = {m.id: m for m in collection(entries, layout=layout)}
        self.assertEqual(machines['probefx'].identity, {'effect': 11, 'page': 21})
        self.assertEqual(machines['probefx'].requested, fixture('probefx').identity)
        self.assertNotEqual(machines['probesynth'].identity['engine'], synth['engine'])
        self.assertEqual({k: v.identity for k, v in machines.items() if k not in ('probefx', 'probesynth')},
                         {k: v for k, v in requested.items() if k not in ('probefx', 'probesynth')})


if __name__ == '__main__':
    unittest.main()
