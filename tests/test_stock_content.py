"""A machine may not be named after the OP-XY's own content (tooling/stock_content.py).

A build installs a machine's screen, presets and samples beside stock's own
content. One named after a stock file would overwrite it, and stock can then halt
at boot. Every path that makes a package refuses such a name.
"""
import contextlib
import copy
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tooling import browser_kit, catalog, custom_preview, definitions, machines, native, stock_content
from tooling.pipeline import config

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/fixtures/machines/probesynth'
STOCK_MENU = ROOT / 'profiles/opxy-1.2.1/content/op1_factory.json'


def renamed_fixture(directory, identity):
    """The probe synth fixture, named `identity` throughout."""
    root = Path(directory) / identity
    shutil.copytree(FIXTURE, root)
    for path in root.iterdir():
        path.write_text(path.read_text(encoding='utf-8').replace('probesynth', identity), encoding='utf-8')
    return root


class StockContentNames(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def refused(self, stock):
        # The rule is per ID, whatever the kind installs, so the message says what the ID names.
        return self.assertRaisesRegex(ValueError, rf"^Machine [a-z_0-9]+: its ID names the OP-XY's own {stock}\. .*Rename it")

    def test_the_profile_lists_stock_content_by_folder(self):
        listed = config()['stock_content']
        self.assertEqual(set(listed), {'display', 'presets', 'samples'})
        self.assertIn('mixer.svg', listed['display'])
        self.assertIn('keys', listed['presets'])
        self.assertIn('kick', listed['samples'])
        for names in listed.values():
            self.assertEqual(names, sorted(names))

    def test_a_screen_named_after_a_stock_screen_is_refused_when_the_definition_loads(self):
        value = json.loads((FIXTURE / 'definition.json').read_text(encoding='utf-8'))
        value['id'] = 'monitor'
        with self.refused('content/display/monitor.svg'):
            definitions.validate_definition(value, 'synth')
        with self.refused('content/display/monitor.svg'):
            machines.machine_at(renamed_fixture(self.directory, 'monitor'))

    def test_a_preset_or_sample_folder_named_after_stock_is_refused(self):
        for identity, stock in (('keys', 'content/presets/keys'), ('strings', 'content/presets/strings'),
                                ('kick', 'content/samples/kick')):
            with self.subTest(identity), self.refused(stock):
                stock_content.check_identity(identity)
        # Every kind: an effect's ID names its aux preset folder and could become an instrument's.
        value = json.loads((ROOT / 'tests/fixtures/machines/probefx/definition.json').read_text(encoding='utf-8'))
        value['id'] = 'lead'
        with self.refused('content/presets/lead'):
            definitions.validate_definition(value, 'effect')

    def test_names_compare_without_regard_to_case(self):
        self.assertEqual(stock_content.stock_file('content/display/Monitor.SVG'), 'content/display/monitor.svg')
        self.assertEqual(stock_content.stock_file('content/display/universnextpro-light.otf'),
                         'content/display/UniversNextPro-Light.otf')
        self.assertEqual(stock_content.stock_file('content/presets/KEYS/default.json'), 'content/presets/keys')
        profile = copy.deepcopy(config())
        profile['stock_content']['display'].append('ProbeSynth.SVG')
        with self.refused('content/display/ProbeSynth.SVG'):
            stock_content.check_identity('probesynth', profile)

    def test_other_names_and_other_places_pass(self):
        for identity in ('probesynth', 'mixers', 'keys2', 'tapes'):
            stock_content.check_identity(identity)
        self.assertIsNone(stock_content.stock_file('content/op1_factory.json'))
        self.assertIsNone(stock_content.stock_file('content/display'))
        self.assertIsNone(stock_content.stock_file('content/aux_presets/cv/init.json'))
        with self.assertRaisesRegex(ValueError, 'lists no stock content names'):
            stock_content.check_identity('probesynth', {})

    def test_build_and_compile_refuse_it_before_compiling(self):
        root = renamed_fixture(self.directory, 'tape')
        for command in ('build', 'compile'):
            with self.subTest(command):
                out, errors = self.directory / ('out-' + command), io.StringIO()
                arguments = ['--out', str(out), '--machine', str(root)]
                if command == 'build':
                    arguments += ['--stock-os', str(self.directory / 'os.ldr')]
                with contextlib.redirect_stderr(errors):
                    self.assertEqual(native.main(arguments, command), 1)
                self.assertIn("Machine tape: its ID names the OP-XY's own content/display/tape.svg", errors.getvalue())
                self.assertFalse(out.exists())

    def test_kit_refuses_it_before_building(self):
        root = renamed_fixture(self.directory, 'mixer')
        tools = self.directory / 'tools'
        tools.mkdir()
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors), self.assertRaises(SystemExit) as exit:
            browser_kit.main(['--stock-os', str(self.directory / 'os.ldr'), '--out', str(self.directory / 'kit'),
                              '--toolchain', str(tools), '--compiler', str(tools), '--personal',
                              '--machine', str(root)])
        self.assertEqual(exit.exception.code, 1)
        self.assertIn("Machine mixer: its ID names the OP-XY's own content/display/mixer.svg", errors.getvalue())
        self.assertFalse((self.directory / 'kit').exists())

    def test_the_files_a_build_installs_are_checked_against_the_profile_it_targets(self):
        m = machines.machine_at(FIXTURE)
        menu = STOCK_MENU.read_bytes()
        self.assertIn('content/display/probesynth.svg', native.prepare_assets(menu, config(), [m]))
        # A file lands over a stock file, or into a stock folder; the message says which.
        profile = copy.deepcopy(config())
        profile['stock_content']['presets'].append('ProbeSynth')
        with self.assertRaisesRegex(ValueError, "^Machine probesynth would install content/presets/probesynth/"
                                                r"default.json into the OP-XY's own folder content/presets/ProbeSynth\. "):
            native.prepare_assets(menu, profile, [m])
        profile = copy.deepcopy(config())
        profile['stock_content']['display'].append('ProbeSynth.svg')
        with self.assertRaisesRegex(ValueError, "^Machine probesynth would install content/display/probesynth.svg "
                                                r"over the OP-XY's own content/display/ProbeSynth.svg\. "):
            native.prepare_assets(menu, profile, [m])

    def test_an_extension_id_is_not_refused_as_it_installs_nothing_under_it(self):
        # A master processor's ID names no content path (its artwork file is checked where the build
        # installs it), so a stock screen's name is a valid extension ID, as in the browser builder.
        value = json.loads((FIXTURE / 'definition.json').read_text(encoding='utf-8'))
        value['resources'] = {'display': value['resources']['display']}
        value['id'] = 'tempo'
        definitions.validate_definition(value, 'master')
        # The same name for a machine is refused.
        with self.refused('content/display/tempo.svg'):
            stock_content.check_identity('tempo')

    def test_sdk_new_refuses_it(self):
        with self.refused('content/display/master.svg'):
            custom_preview.custom_identity('master')

    def test_the_catalog_refuses_it_for_a_machine_that_can_be_built(self):
        entry = {'id': 'tape', 'kind': 'synth', 'maintainer': 'someone', 'status': 'reserved'}
        with self.refused('content/display/tape.svg'):
            catalog.validate({'schema': 1, 'machines': [entry]})
        # A retired machine is never built again; its ID stays held as it is.
        catalog.validate({'schema': 1, 'machines': [dict(entry, status='retired')]})

    def test_every_buildable_machine_in_the_catalog_passes(self):
        entries = json.loads((ROOT / 'catalog/machines.json').read_text(encoding='utf-8'))['machines']
        catalog.validate({'schema': 1, 'machines': entries})
        colliding = set()
        for entry in entries:
            try:
                stock_content.check_identity(entry['id'])
            except ValueError:
                colliding.add((entry['id'], entry['status']))
        # One retired machine shares its name with a stock preset folder; nothing built does.
        self.assertEqual(colliding, {('pluck', 'retired')})
        for fixture in sorted((ROOT / 'tests/fixtures/machines').iterdir()):
            stock_content.check_identity(json.loads((fixture / 'definition.json').read_text(encoding='utf-8'))['id'])


if __name__ == '__main__':
    unittest.main()
