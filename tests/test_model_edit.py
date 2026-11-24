"""A drum kit's value editing across models (resources.modelEdit, SDK 1.25): its declaration,
what it generates, and that a build without such a kit generates and compiles what it did.
The shared editor's turns run in tests/fixtures/drum_gui.cpp (test_drum_services)."""
import copy
import json
import re
from pathlib import Path
import shutil
import tempfile
import unittest

from machine_fixtures import FOLDER, fixture
from tooling import adapters, native
from tooling.definitions import SDK_1_25, validate_definition
from tooling.machines import machine_at
from tooling.pipeline import ROOT
from test_divide import blackfin_compiler

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


def kit(**resources):
    definition = copy.deepcopy(fixture('probekit21').definition)
    definition['resources'].update(resources)
    return definition


class Declaration(unittest.TestCase):
    def test_model_edit_is_a_kits_0_or_1_in_place_of_parameter_edit(self):
        for value in (0, 1):
            validate_definition(kit(modelEdit=value), 'drum')
        for value in (2, -1, True, '1', 1.0):
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_definition(kit(modelEdit=value), 'drum')
        with self.assertRaisesRegex(ValueError, "takes parameterEdit's place"):
            validate_definition(kit(modelEdit=1, parameterEdit=1), 'drum')
        validate_definition(kit(modelEdit=0, parameterEdit=1), 'drum')
        # Every other kit's option goes with it.
        validate_definition(kit(modelEdit=1, modelGroups=[dict(label='A', count=1), dict(label='B', count=1)],
                                padMix=1, chokeSameKey=1), 'drum')
        validate_definition(kit(modelEdit=1, padVolume=1, padRandom=1, ampEnvelope=1), 'drum')
        for name, kind in (('probesynth', 'synth'), ('probefx', 'effect'), ('probesample', 'sample')):
            definition = copy.deepcopy(fixture(name).definition)
            definition['resources']['modelEdit'] = 1
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                validate_definition(definition, kind)

    def test_it_needs_sdk_1_25_and_the_fixture_kit_declares_it(self):
        self.assertEqual(SDK_1_25, (1, 25))
        self.assertTrue(adapters.model_edit(fixture('probekit25')))
        self.assertFalse(adapters.model_edit(fixture('probekit21')))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'probekit25'
            shutil.copytree(FOLDER / 'probekit25', folder)
            record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
            for version, ok in (('1.24', False), ('1.23', False), ('1.25', True)):
                record['sdk'] = version
                (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
                with self.subTest(version=version):
                    if ok:
                        machine_at(folder)
                    else:
                        with self.assertRaisesRegex(ValueError, 'modelEdit'):
                            machine_at(folder)


class Generation(unittest.TestCase):
    def test_only_a_model_edit_kit_gets_the_editor_and_the_table(self):
        new, old = fixture('probekit25'), fixture('probekit21')
        with tempfile.TemporaryDirectory() as directory:
            voice = adapters.generate_voice(directory, new).read_text(encoding='utf-8')
            old_voice = adapters.generate_voice(directory, old).read_text(encoding='utf-8')
            both = adapters.generate_drum_callbacks(directory, [old, new]).read_text(encoding='utf-8')
            alone = adapters.generate_drum_callbacks(directory, [old]).read_text(encoding='utf-8')
        wrapper = ('opxy::u32 probekit25_drum_edit_model(opxy::u32 model, opxy::u32 slot, opxy::u32 value, '
                   'opxy::i32 delta, opxy::u32 speed) {\n'
                   '    return probekit25::edit_model_parameter(model, slot, value, delta, speed);\n}\n')
        self.assertIn(wrapper, voice)
        self.assertNotIn('_drum_edit_model', old_voice)
        # An older kit's adapter is the template's expansion with the placeholder empty.
        template = (PLATFORM / 'drum_voice.cpp.in').read_text(encoding='utf-8')
        self.assertEqual(template.count('@MODEL_EDIT@'), 1)
        self.assertIn('#endif\n@MODEL_EDIT@void @PREFIX@_drum_scramble_range', template)
        table = ('#include "drum_model_edit.hpp"\n'
                 'extern "C" opxy::u32 probekit25_drum_edit_model(opxy::u32, opxy::u32, opxy::u32, opxy::i32, '
                 'opxy::u32) __asm__("probekit25_drum_edit_model");\n'
                 'const opxy::platform::DrumModelEdit sdk_drum_model_edits[] = {\n'
                 '    {probekit25_engine_id, probekit25_drum_edit_model},\n};\n'
                 'const opxy::u32 sdk_drum_model_edit_count = 1;\n')
        self.assertIn(table, both)
        self.assertNotIn('sdk_drum_model_edits', alone)
        self.assertEqual(adapters.model_edit_table([old, fixture('probesynth')]), '')
        # An unselected kit's editor, which the table names, resolves to the builder's kit_absent.
        self.assertEqual(native.absent_symbols(new)['probekit25_drum_edit_model'], 'kit_absent')
        self.assertNotIn('probekit21_drum_edit_model', native.absent_symbols(old))

    def test_only_a_build_with_such_a_kit_compiles_the_shared_editor_with_the_define(self):
        new, old = fixture('probekit25'), fixture('probekit21')
        self.assertEqual(native.sdk_defines([old, fixture('probesynth')]), ())
        self.assertEqual(native.sdk_defines([old, new]), (native.MODEL_EDIT_DEFINE,))
        self.assertEqual(native.sdk_defines([new, fixture('probepads')]),
                         (native.PLAYER_INSTRUMENT_DEFINE, native.MODEL_EDIT_DEFINE))
        source = (PLATFORM / 'drum_synthesis_stock.cpp').read_text(encoding='utf-8')
        # One include and one arm of the editor's chain, both under the define.
        self.assertEqual(source.count('#if OPXY_MODEL_EDIT'), 2)
        self.assertIn('#if OPXY_MODEL_EDIT\n#include "drum_model_edit.hpp"\n#endif\n', source)
        arm = source[source.index('#if OPXY_MODEL_EDIT\n    } else if'):]
        arm = arm[:arm.index('#endif\n') + len('#endif\n')]
        self.assertIn('drum_model_turn(*machine, *across, slot, delta, static_cast<i32>(event[2]), model, controls)',
                      arm)
        self.assertTrue(source.count(arm) == 1 and arm.endswith('            return 0;\n#endif\n'))


def without(source, define):
    """The source with every `#if <define>` block taken out, as before the SDK 1.25 additions."""
    return re.sub(r'#if %s\n.*?#endif\n' % define, '', source, flags=re.S)


class ZeroCost(unittest.TestCase):
    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_without_the_defines_the_shared_units_compile_as_without_the_additions(self):
        # A build without a 1.25 player or a modelEdit kit compiles players.cpp and
        # drum_synthesis_stock.cpp to the same object as their text with the blocks removed.
        compiler = blackfin_compiler()
        assembler = compiler.with_name('bfin-elf-as' + compiler.suffix)
        for unit, define in (('players.cpp', 'OPXY_PLAYER_INSTRUMENT'), ('drum_synthesis_stock.cpp', 'OPXY_MODEL_EDIT')):
            text = (PLATFORM / unit).read_text(encoding='utf-8')
            self.assertIn('#if ' + define, text)
            objects = []
            with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
                for folder, source in ((a, text), (b, without(text, define))):
                    path = Path(folder) / unit
                    path.write_text(source, encoding='utf-8', newline='\n')
                    obj = native.compile_cxx(compiler, assembler, path, Path(folder) / 'unit.o', native.includes())
                    objects.append(obj.read_bytes())
            with self.subTest(unit=unit):
                self.assertNotEqual(without(text, define), text)
                self.assertEqual(objects[0], objects[1])


if __name__ == '__main__':
    unittest.main()
