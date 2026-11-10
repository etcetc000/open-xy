"""A synth's Shift row (SDK 1.13): its definition, generated adapters and packing."""
import copy
from dataclasses import replace
from fractions import Fraction
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import adapters
from tooling import native
from tooling.definitions import (shift_choices, shift_field, shift_preset_params, shift_q31, shift_words, synth_shift,
                                 validate_definition)
from tooling.firmware import active_services
from tooling.machines import check_preset, machine_at
from tooling.pipeline import config
from machine_fixtures import FOLDER, fixture

ROOT = Path(__file__).resolve().parents[1]


def amount(i, default=0):
    return dict(id=f's{i}', label=f'S {i}', default=default, display='percent',
                mapping=dict(kind='linear', minimum=0, maximum=1))


def enum(i, count):
    return dict(id=f'e{i}', label=f'E {i}', default=0, display='enum',
                mapping=dict(kind='enum', labels=[f'c{n}' for n in range(count)]))


ABSENT = dict(id='gap', label='', default=0, display='percent', absent=True,
              mapping=dict(kind='linear', minimum=0, maximum=1))


class Definition(unittest.TestCase):
    def setUp(self):
        self.paged = copy.deepcopy(fixture('probeshift').definition)
        self.plain = copy.deepcopy(fixture('probesynth').definition)

    def test_the_fixture_has_two_pages_and_a_shift_row_under_each(self):
        m = fixture('probeshift')
        self.assertEqual(len(m.definition['parameters']), 8)
        self.assertEqual([shift_choices(p) for p in synth_shift(m.definition)], [0, 0, 5, 0, 0, 1, 0, 0])
        # The row's defaults as render receives them: a choice at its bucket's middle,
        # an amount in hundredths of full scale.
        self.assertEqual(shift_words(m.definition),
                         [0, 536870912, 1073741822, 0, 0, 0, 1073741824, 1073741824])

    def test_a_four_control_synth_may_have_four_entries(self):
        definition = dict(self.plain, shift=[amount(0), enum(1, 128), ABSENT, amount(3)])
        validate_definition(definition, 'synth')

    def test_invalid_rows_are_refused(self):
        rejected = {
            'nine entries': dict(self.paged, shift=[amount(i) for i in range(9)]),
            'a position of its own': dict(self.paged, shift=[dict(amount(0), control=1)]),
            'too many choices': dict(self.paged, shift=[enum(0, 129)]),
            'a division': dict(self.paged, shift=[dict(amount(0), display='division')]),
            'a duplicate id': dict(self.paged, shift=[dict(amount(0), id='blend')]),
            'only absent entries': dict(self.paged, shift=[ABSENT]),
            'empty': dict(self.paged, shift=[]),
            'a second page without one': dict(self.plain, shift=[amount(i) for i in range(5)]),
        }
        # Five to eight controls without "press": "page" alternate per position: no pages.
        alternates = dict(self.paged, shift=[amount(0)])
        del alternates['press']
        rejected['per-position alternates'] = alternates
        controlled = copy.deepcopy(self.plain)
        controlled['shift'] = [amount(0)]
        controlled['resources']['trackControl'] = {'stateBytes': 4, 'stateAlignment': 4, 'unique': True, 'aux': [12]}
        rejected['a track control machine'] = controlled
        for name, definition in rejected.items():
            with self.subTest(name), self.assertRaises(ValueError):
                validate_definition(definition, 'synth')

    def test_presets_may_set_the_row(self):
        page = next(iter(self.paged['presets'].values()))
        validate_definition(dict(self.paged, presets={'Row': dict(page, noise=0.5, octave=1)}), 'synth')
        for extra in (dict(gap=0), dict(unknown=0.5)):
            with self.subTest(extra), self.assertRaises(ValueError):
                validate_definition(dict(self.paged, presets={'Bad': dict(page, **extra)}), 'synth')
        # An effect's shift row stays out of its presets.
        effect = dict(fixture('probefx').definition, shift=[enum(0, 3)])
        preset = dict({p['id']: p['default'] for p in effect['parameters']}, e0=0)
        with self.assertRaises(ValueError):
            validate_definition(dict(effect, presets={'Bad': preset}), 'effect')

    def test_the_preset_region_holds_the_row_as_a_drum_key(self):
        definition = fixture('probeshift').definition
        values = {'noise': 0.3, 'octave': 0.75, 'balance': 0.2}
        params = shift_preset_params(definition, values)
        self.assertEqual(len(params), 4)
        self.assertTrue(all(0 <= p <= 0x7fff for p in params))
        # drum_storage.hpp drum_put/drum_get: 15 bits a word, 7 a field, model 0 in 56..59.
        bits = sum(p << (15 * w) for w, p in enumerate(params))
        fields = [bits >> (7 * i) & 127 for i in range(8)]
        self.assertEqual(fields, [shift_field(p, values.get(p['id'], p['default'])) for p in synth_shift(definition)])
        self.assertEqual(bits >> 56, 0)

    def test_own_presets_keep_the_row_in_sample_less_regions(self):
        preset = dict(version=4, platform='OP-XY', type='probeshift', engine=dict(params=[0] * 8),
                      regions=[dict(model='fm slap', params=[1, 2, 3, 4], sample='')] * 24)
        check_preset(preset, 'probeshift', shift=True)
        for bad in ([dict(sample='content/samples/a.wav')], [dict()] * 25):
            with self.subTest(len(bad)), self.assertRaises(ValueError):
                check_preset(dict(preset, regions=bad), 'probeshift', shift=True)
        with self.assertRaises(ValueError):
            check_preset(preset, 'probeshift')

    def test_its_presets_and_locks_use_the_kits_services(self):
        profile = config()
        users = native.service_users([fixture('probeshift')])
        self.assertIn('synthShift', users)
        self.assertTrue({'drums', 'drum_locks'} <= active_services(profile, users))
        self.assertFalse({'drums', 'drum_locks'} & active_services(profile, native.service_users([fixture('probesynth')])))
        symbols = {p['address']: p['symbol'] for p in profile['patches'] if p.get('service') == 'drums'}
        # Export's region choice packs the row first; import's neutral offsets stay a kit's.
        self.assertEqual(symbols['0x8d4e74b0'], 'extensions_preset_export_regions')
        self.assertEqual(symbols['0x8d4e39c0'], 'extensions_drum_compare_r0')
        hooks = (ROOT / 'sdk/platform/opxy-1.2.1/drum_hooks.s').read_text()
        for stub in ('extensions_drum_preset_import', 'extensions_drum_export_empty',
                     'extensions_drum_export_parameters', 'extensions_drum_export_gain'):
            body = hooks[hooks.index(stub + ':'):hooks.index('.size ' + stub)]
            self.assertIn('PRESET_ALIAS_R0', body, stub)
        compare = hooks[hooks.index('extensions_drum_compare_r0:'):hooks.index('.size extensions_drum_compare_r0')]
        self.assertIn('DRUM_ALIAS_R0', compare)

    def test_it_needs_sdk_1_13(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'probeshift'
            shutil.copytree(FOLDER / 'probeshift', folder)
            record = json.loads((folder / 'machine.json').read_text())
            record['sdk'] = '1.12'
            (folder / 'machine.json').write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, '1.13'):
                machine_at(folder)


class Packing(unittest.TestCase):
    def test_amounts_step_in_hundredths(self):
        p = amount(0)
        for field in range(101):
            word = shift_q31(p, field)
            self.assertLessEqual(abs(Fraction(word) - Fraction(field * 0x7fffffff, 100)), Fraction(1, 2))
            # header_amount shows the step itself, and the step reads back from its word.
            self.assertEqual(((word >> 10) * 100 + (1 << 20)) >> 21, field)
            self.assertEqual(shift_field(p, word / 0x7fffffff), field)

    def test_choices_read_back_as_any_enum(self):
        for count in range(2, 129):
            p = enum(0, count)
            for choice in range(count):
                word = shift_q31(p, choice)
                self.assertEqual(((word >> 16) * count) >> 15, choice)
                self.assertEqual(shift_field(p, word / 0x7fffffff), choice)

    @unittest.skipUnless(shutil.which('node'), 'Node is not installed')
    def test_the_browser_stores_entries_as_the_device_does(self):
        script = ('import {shiftWord} from %s;\nconst out = [];\nfor (let i = 0; i <= 200; ++i) {\n'
                  '  out.push(shiftWord({mapping:{kind:"linear"}}, i / 200));\n'
                  '  for (const n of [2, 5, 128]) out.push(shiftWord({mapping:{kind:"enum", labels:Array(n).fill("x")}}, i / 200));\n'
                  '}\nconsole.log(JSON.stringify(out));\n') % json.dumps((ROOT / 'sdk/browser/controls.mjs').as_uri())
        result = subprocess.run(['node', '--input-type=module', '-e', script], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = []
        for i in range(201):
            expected.append(shift_q31(amount(0), shift_field(amount(0), i / 200)))
            for n in (2, 5, 128):
                expected.append(shift_q31(enum(0, n), shift_field(enum(0, n), i / 200)))
        self.assertEqual(json.loads(result.stdout), expected)

    def test_the_host_packing_matches(self):
        from test_sample_services import HOST_CXX
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        # The defaults and choices as the generator writes them (adapters.shift_row_tables),
        # and the words Python says they play.
        p = [amount(0), amount(1, 0.25), dict(enum(2, 5), default=0.5), amount(3)]
        defaults = [shift_field(e, e['default']) for e in p] + [0] * 4
        expected = [shift_q31(e, field) for e, field in zip(p, defaults)]
        # A preset's region params, as Python writes them and the firmware packs them.
        definition = fixture('probeshift').definition
        values = {'noise': 0.3, 'octave': 0.75, 'balance': 0.2}
        fields = [shift_field(e, values.get(e['id'], e['default'])) for e in synth_shift(definition)]
        params = shift_preset_params(definition, values)
        source = r'''#include "synth_shift.hpp"
#include "drum_storage.hpp"
#include <cstdio>
using namespace opxy;
namespace row = opxy::platform::synth_shift;
int main() {
    const u8 defaults[8] = {%s}, choices[8] = {0, 0, 5, 0, 0, 1, 0, 0};
    const i32 expected[4] = {%s};
    const u8 fields[8] = {%s};
    const u32 params[4] = {%s};
    // A word without the mark plays the defaults.
    for (u32 i = 0; i < 4; ++i)
        if (row::q31(row::field(0, i, defaults), choices[i]) != expected[i]) return 1;
    // Every value of every entry survives packing beside the others.
    i32 word = 0;
    for (u32 entry = 0; entry < 8; ++entry)
        for (u32 value = 0; value <= row::top(choices[entry]); ++value) {
            const i32 next = row::with(word, entry, value, defaults);
            if (!(next & row::kMark) || next < 0) return 2;
            if (row::field(next, entry, defaults) != value) return 3;
            for (u32 other = entry / 4 * 4; other < entry / 4 * 4 + 4; ++other)
                if (other != entry && row::field(next, other, defaults) != row::field(word, other, defaults)) return 4;
            word = entry < 4 ? next : word;
        }
    for (u32 f = 0; f <= 100; ++f) {
        const i32 q = row::amount(f);
        if (((q >> 10) * 100 + (1 << 20)) >> 21 != static_cast<i32>(f)) return 5;
    }
    for (u32 n = 2; n <= 128; ++n)
        for (u32 c = 0; c < n; ++c)
            if ((((static_cast<u32>(row::q31(c, static_cast<u8>(n))) >> 16) * n) >> 15) != c) return 6;
    // The preset region: drum_pack's words are the params << 16, and unpack gives the fields back.
    i32 words[4];
    if (!opxy::platform::drum_pack(0, fields, words)) return 7;
    for (u32 i = 0; i < 4; ++i)
        if (static_cast<u32>(words[i]) != params[i] << 16) return 8;
    u8 model = 9, back[8];
    if (!opxy::platform::drum_unpack(words, &model, back) || model) return 9;
    for (u32 i = 0; i < 8; ++i)
        if (back[i] != fields[i]) return 10;
    return 0;
}
''' % (', '.join(map(str, defaults)), ', '.join(map(str, expected)), ', '.join(map(str, fields)),
       ', '.join(f'{p}u' for p in params))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'test.cpp').write_text(source)
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                       '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'), str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, 'step %d' % result.returncode)


class Generation(unittest.TestCase):
    def test_the_voice_adapter_passes_the_row_as_parameters_8_to_15(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_voice(Path(directory), fixture('probeshift')).read_text()
            plain = adapters.generate_voice(Path(directory), fixture('probesynth')).read_text()
        self.assertIn('#include "synth_shift.hpp"', text)
        self.assertIn('static const opxy::u8 kShiftDefaults[8] = {0, 25, 2, 0, 0, 0, 50, 50};', text)
        self.assertIn('static const opxy::u8 kShiftTops[8] = {100, 100, 4, 100, 100, 0, 100, 100};', text)
        self.assertIn('static const opxy::u32 kShiftBuckets[8] = {0u, 0u, 429496729u, 0u, 0u, 0u, 0u, 0u};', text)
        self.assertIn('        opxy::i32 controls[2 * opxy::parameter_slots];\n', text)
        self.assertIn('        shift_words(parameters, controls + opxy::parameter_slots, shift_kept[slot - voices]);\n', text)
        # A synth without a row keeps its adapter as before.
        self.assertNotIn('shift', plain.lower().replace('shifted', ''))
        self.assertIn('        const opxy::i32* controls = parameters;\n', plain)

    def test_a_profiling_build_keeps_the_row_from_the_track(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_voice(Path(directory), fixture('probeshift'), sweep=True).read_text()
        self.assertIn('const opxy::i32* stock_controls = swept ? swept : parameters;', text)
        self.assertIn('shift_words(parameters, controls + opxy::parameter_slots, shift_kept[slot - voices]);', text)

    def test_the_control_table_describes_the_row(self):
        with tempfile.TemporaryDirectory() as directory:
            table = adapters.generate_synth_controls(Path(directory), [fixture('probeshift'), fixture('probesynth')]).read_text()
        self.assertIn('static const char* const probeshift_shift_titles[] = '
                      '{"noise", "drift", "octave", "lowcut", "slide", "", "ratio", "balance"};', table)
        self.assertIn('static const opxy::platform::SynthShift probeshift_shift_row = '
                      '{8, {0, 0, 5, 0, 0, 1, 0, 0}, {0, 25, 2, 0, 0, 0, 50, 50}, probeshift_shift_titles};', table)
        self.assertIn('probeshift_format, false, true, 0, 0, 0, {1, 1, 1, 1}, nullptr, {0, 0, 0, 0}, &probeshift_shift_row},', table)
        # Entries caption as slots 8..15; the absent one (13) has no caption.
        format_ = table[table.index('static bool probeshift_format'):table.index('static bool probesynth_format')]
        for slot in (8, 9, 10, 11, 12, 14, 15):
            self.assertIn(f'case {slot}', format_)
        self.assertNotIn('case 13', format_)
        self.assertIn('probesynth_format, false, false, 0, 0},', table)

    def test_the_preview_renders_sixteen_words(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_voice_preview(Path(directory), fixture('probeshift').definition).read_text()
            plain = adapters.generate_voice_preview(Path(directory), fixture('probesynth').definition).read_text()
        self.assertIn('opxy::u32 opxy_parameter_count() { return 2 * opxy::parameter_slots; }', text)
        self.assertIn('opxy::u32 opxy_parameter_count() { return opxy::parameter_slots; }', plain)

    def test_an_effects_shift_row_is_unchanged(self):
        definition = dict(fixture('probefx').definition, shift=[enum(0, 3)])
        effect = replace(fixture('probefx'), definition=definition)
        self.assertEqual(synth_shift(definition), [])
        with tempfile.TemporaryDirectory() as directory:
            table = adapters.generate_synth_controls(Path(directory), [effect]).read_text()
        self.assertIn('probefx_format, true, false, 0, 0, 1, {3, 1, 1, 1}, probefx_shift_titles},', table)


if __name__ == '__main__':
    unittest.main()
