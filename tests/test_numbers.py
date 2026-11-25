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
from tooling import records
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
        identity['engine'] = 72 if identity['engine'] != 72 else 71
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
    generate_startup(shared)
    for m in machines:
        records.generate(directory / m.id, m)
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in sorted(directory.rglob('*')) if p.is_file()}


class LinkTimeNumbers(unittest.TestCase):
    def test_a_machine_left_out_of_a_link_has_no_numbers(self):
        from tooling import native
        _profile, machines, definitions = native.configuration(fixture_entries())
        # Each number symbol configuration defines for a machine becomes 0 when it is left out.
        for m in machines:
            with self.subTest(machine=m.id):
                numbers = {k for k in definitions if k.startswith(m.prefix + '_') and k.endswith('_id') and
                           k[len(m.prefix):] in ('_engine_id', '_page_id', '_screen_page_id', '_fx_id', '_fx_page_id',
                                                 '_player_id')}
                self.assertTrue(numbers)
                self.assertEqual(native.absent_numbers([m]), dict.fromkeys(numbers, 0))
        # A sample engine drawing its own screen also gives up its screen page.
        sample = next(m for m in machines if m.kind == 'sample')
        own = replace(sample, display='custom')
        self.assertEqual(set(native.absent_numbers([own])), {sample.prefix + s for s in ('_engine_id', '_page_id', '_screen_page_id')})
        # By kind, not by name: an ID extending another's with "_fx" keeps its own symbols apart.
        synth = next(m for m in machines if m.kind == 'synth')
        self.assertNotIn(synth.prefix + '_fx_page_id', native.absent_numbers([synth]))

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

    def test_an_engine_number_among_the_effect_numbers_is_given_up_once(self):
        # A layout an earlier SDK wrote may hold an engine at an effect number; the machine keeps its
        # page and is given an engine number, which the next layout keeps.
        entries = fixture_entries()
        effects = config()['identities']['effects']
        earlier = {'probesynth': {'engine': 40, 'page': 92}, 'gone': {'engine': 41, 'page': 120}}
        machines = {m.id: m for m in collection(entries, layout=earlier)}
        synth = machines['probesynth'].identity
        self.assertEqual(synth['page'], 92)
        self.assertNotIn(synth['engine'], effects)
        for m in machines.values():
            self.assertFalse('engine' in m.identity and m.identity['engine'] in effects and m.id not in FIXED_NUMBERS, m.id)
        fixed = [(name, None, identity) for name, identity in FIXED_NUMBERS.items()]
        rules = config()['identities']
        requests = [(m.id, m.kind, m.requested if m.requested is not None else m.identity) for m in machines.values()]
        after = number_layout(rules, fixed, earlier, requests)
        self.assertEqual(after['gone']['page'], 120)
        self.assertNotIn(after['gone']['engine'], effects)
        self.assertEqual(number_layout(rules, fixed, after, requests), after)

    def test_machines_joining_a_layout_never_move_its_machines(self):
        # As a pack's machines join a site's (builder/core.mjs importMachinePack): numbers a layout
        # gave stay, and the one-time move off the effect numbers is the same with or without them.
        rules = config()['identities']
        fixed = [(name, None, identity) for name, identity in FIXED_NUMBERS.items()]
        site = [('drone', 'synth', {'engine': None, 'page': None}), ('echo', 'effect', {'effect': None, 'page': 21}),
                ('keys', 'sample', {'engine': None, 'page': 2})]
        earlier = {'drone': {'engine': 40, 'page': 120}, 'echo': {'effect': 56, 'page': 21}, 'keys': {'engine': 70, 'page': 2}}
        joining = [('a' + str(i), 'synth', {'engine': None, 'page': None}) for i in range(40)]
        joining += [('e' + str(i), 'effect', {'effect': None, 'page': 21}) for i in range(10)]
        alone = number_layout(rules, fixed, earlier, site)
        together = number_layout(rules, fixed, earlier, site + joining)
        self.assertEqual({k: together[k] for k in alone}, alone)
        self.assertEqual({k: number_layout(rules, fixed, alone, site + joining)[k] for k in alone}, alone)
        for name, identity in together.items():
            if name not in FIXED_NUMBERS and 'engine' in identity:
                self.assertNotIn(identity['engine'], rules['effects'], name)


BROWSER = """
import {numberLayout} from %s;
let input = '';
for await (const chunk of process.stdin) input += chunk;
const {rules, fixed, cases} = JSON.parse(input);
process.stdout.write(JSON.stringify(cases.map(c => {
  try { return numberLayout(rules, fixed, c.layout, c.machines); } catch (error) { return {error: error.message}; }
})));
"""


class BrowserParity(unittest.TestCase):
    def test_python_and_the_builder_give_the_same_numbers(self):
        # Generated layouts and selections, earlier SDKs' layouts with engines at effect numbers
        # among them: tooling.machines.number_layout and builder/core.mjs numberLayout agree.
        import random
        import subprocess
        rules = config()['identities']
        fixed = [(name, None, identity) for name, identity in FIXED_NUMBERS.items()]
        generator = random.Random(1)
        names = ['m%d' % i for i in range(60)] + ['drone', 'echo', 'pluck', 'choir_36', 'c115']
        engines = list(range(34, 256))

        def machine(name, held):
            # Mostly the kind its layout entry says, sometimes another.
            kind = generator.choice(('synth', 'drum', 'sample', 'effect', 'player'))
            if held and generator.random() < 0.9:
                kind = ('player' if 'player' in held else 'effect' if 'effect' in held else
                        'sample' if held['page'] in (2, 7) else generator.choice(('synth', 'drum', 'sample')))
            if kind == 'effect':
                return [name, kind, {'effect': generator.choice((None, *rules['effects'])), 'page': generator.choice((21, None))}]
            if kind == 'player':
                return [name, kind, {'player': None, 'page': None}]
            if kind == 'sample':
                return [name, kind, {'engine': generator.choice((None, *engines)), 'page': generator.choice((2, 7, None))}]
            page = generator.choice((None, generator.randrange(82, 256)))
            return [name, kind, {'engine': None if page is None else generator.choice(engines), 'page': page}]

        cases = []
        for _ in range(300):
            # Distinct numbers apart from the fixed machines', as a firmware's layout holds them.
            numbers, pages = generator.sample(range(36, 256), 40), generator.sample(range(84, 256), 40)
            effects = generator.sample([n for n in (2, 3, *rules['effects']) if n not in (1, 26, 34, 35)], 30)
            players = generator.sample(rules['players'], 20)
            layout = {}
            for name in generator.sample(names, generator.randrange(0, 30)):
                pick = generator.random()
                if pick < 0.6:
                    layout[name] = {'engine': numbers.pop(), 'page': generator.choice((2, 7, pages.pop()))}
                elif pick < 0.9:
                    layout[name] = {'effect': effects.pop(), 'page': generator.choice((21, 21, pages.pop()))}
                else:
                    layout[name] = {'player': players.pop(), 'page': pages.pop()}
            machines = [machine(name, layout.get(name)) for name in generator.sample(names, generator.randrange(0, 40))]
            cases.append({'layout': layout or None, 'machines': machines})
        script = BROWSER % json.dumps((ROOT / 'builder/core.mjs').as_uri())
        run = subprocess.run(['node', '--input-type=module', '-e', script], capture_output=True, text=True, cwd=ROOT,
                             input=json.dumps({'rules': rules, 'fixed': fixed, 'cases': cases}), timeout=120, check=True)
        browser = json.loads(run.stdout)
        numbered = 0
        for case, theirs in zip(cases, browser, strict=True):
            try:
                ours = number_layout(rules, fixed, case['layout'], case['machines'])
            except ValueError:
                ours = None
            with self.subTest(case=case):
                if ours is None:
                    self.assertIn('error', theirs)
                else:
                    numbered += 1
                    self.assertEqual(ours, theirs)
        # Enough of them number without a clash to compare numbers, not only refusals.
        self.assertGreater(numbered, 100)


if __name__ == '__main__':
    unittest.main()
