"""Track control: the definition resource, the generated table and the reviewed GUI-tick hook."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.adapters import (control_callbacks, generate_control, generate_track_control_overlay,
                              generate_track_controls)
from tooling.definitions import validate_definition
from tooling.machines import check_saved_budget
from tooling.formats import ldr, mapped_offset, sha
from tooling.pipeline import ROOT, config
from test_sample_services import HOST_CXX
from machine_fixtures import fixture


def with_control(definition, control):
    value = copy.deepcopy(definition)
    value['resources']['trackControl'] = control
    return value


class TrackControl(unittest.TestCase):
    def test_definition_declares_aligned_state(self):
        synth = fixture('probesynth').definition
        validate_definition(with_control(synth, {'stateBytes': 8, 'stateAlignment': 4}), 'synth')
        validate_definition(with_control(synth, {'stateBytes': 8, 'stateAlignment': 4, 'unique': True,
                                                 'savedBytes': 8192}), 'synth')
        validate_definition(with_control(synth, {'stateBytes': 8, 'stateAlignment': 4, 'unique': False,
                                                 'marks': {'capture': 1, 'line': 0}}), 'synth')
        for control in ({'stateBytes': 6, 'stateAlignment': 4}, {'stateBytes': 0, 'stateAlignment': 4},
                        {'stateBytes': 8}, {'stateBytes': 8, 'stateAlignment': 4, 'tracks': 1}, 1,
                        {'stateBytes': 8, 'stateAlignment': 4, 'unique': True, 'savedBytes': 0},
                        {'stateBytes': 8, 'stateAlignment': 4, 'unique': True, 'savedBytes': 8193},
                        # Data kept with projects is a unique machine's.
                        {'stateBytes': 8, 'stateAlignment': 4, 'savedBytes': 8},
                        {'stateBytes': 8, 'stateAlignment': 4, 'unique': False, 'savedBytes': 8},
                        {'stateBytes': 8, 'stateAlignment': 4, 'unique': 1},
                        {'stateBytes': 8, 'stateAlignment': 4, 'marks': {'capture': 0, 'line': 1}},
                        {'stateBytes': 8, 'stateAlignment': 4, 'marks': {'capture': 17, 'line': 1}},
                        {'stateBytes': 8, 'stateAlignment': 4, 'marks': {'capture': 1, 'line': 17}},
                        {'stateBytes': 8, 'stateAlignment': 4, 'marks': {'capture': 1}}):
            with self.subTest(control=control), self.assertRaises(ValueError):
                validate_definition(with_control(synth, control), 'synth')
        effect = fixture('probefx').definition
        with self.assertRaises(ValueError):
            validate_definition(with_control(effect, {'stateBytes': 8, 'stateAlignment': 4}), 'effect')

    def test_collection_without_track_control_links_none_of_it(self):
        machines = [fixture(name) for name in ('probesynth', 'probefx')]
        self.assertEqual([control_callbacks(m) for m in machines], [[], []])
        self.assertNotIn('probesynth/control.o', native.object_owners(machines))
        self.assertNotIn('sdk/track_control.o', native.object_owners(machines))
        self.assertNotIn('sdk/track_control_table.o', native.object_owners(machines))
        with tempfile.TemporaryDirectory(prefix='track-control-') as folder:
            table = generate_track_controls(folder, machines).read_text(encoding='utf-8')
        self.assertIn('{nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, false}', table)
        self.assertNotIn('probesynth', table)

    def test_a_machine_that_keeps_data_exports_save_and_load(self):
        synth = copy.deepcopy(fixture('probesynth'))
        synth.definition['resources']['trackControl'] = {'stateBytes': 8, 'stateAlignment': 4, 'unique': True,
                                                         'savedBytes': 100}
        self.assertEqual(control_callbacks(synth)[2:], [synth.prefix + '_control_save', synth.prefix + '_control_load'])
        with tempfile.TemporaryDirectory(prefix='track-control-') as folder:
            table = generate_track_controls(folder, [synth]).read_text(encoding='utf-8')
            binding = generate_control(folder, synth).read_text(encoding='utf-8')
        p = synth.prefix
        self.assertIn(f'"{synth.id}", 100, {p}_control_save, {p}_control_load, true}}', table)
        self.assertIn('ControlState states[1];', binding)
        self.assertIn('control_save(states[0], data, 100)', binding)
        self.assertIn('return bytes <= 100 ? bytes : 0;', binding)

    def test_a_build_keeps_at_most_16384_bytes_with_a_project(self):
        def keeping(name, saved):
            synth = copy.deepcopy(fixture('probesynth'))
            synth.definition['resources']['trackControl'] = {'stateBytes': 8, 'stateAlignment': 4, 'unique': True,
                                                             'savedBytes': saved}
            return synth
        check_saved_budget([keeping('one', 8192), keeping('two', 8192), fixture('probesynth')])
        with self.assertRaisesRegex(ValueError, 'more than the build limit of 16384'):
            check_saved_budget([keeping('one', 8192), keeping('two', 8192), keeping('three', 1)])

    def test_each_copy_of_a_machine_that_is_not_unique_has_its_own_state(self):
        synth = copy.deepcopy(fixture('probesynth'))
        synth.definition['resources']['trackControl'] = {'stateBytes': 8, 'stateAlignment': 4}
        with tempfile.TemporaryDirectory(prefix='track-control-') as folder:
            table = generate_track_controls(folder, [synth]).read_text(encoding='utf-8')
            binding = generate_control(folder, synth).read_text(encoding='utf-8')
        self.assertIn('nullptr, nullptr, false}', table)
        self.assertIn('ControlState states[8];', binding)
        self.assertIn('control_reset(states[copy])', binding)
        self.assertIn('control(states[copy], *input, *overlay)', binding)

    def test_a_machine_can_draw_its_own_marks(self):
        import dataclasses
        import shutil
        plain = copy.deepcopy(fixture('probesynth'))
        plain.definition['resources']['trackControl'] = {'stateBytes': 8, 'stateAlignment': 4}
        with tempfile.TemporaryDirectory(prefix='track-control-') as folder:
            root = Path(folder) / 'own'
            shutil.copytree(fixture('probesynth').root, root)
            definition = copy.deepcopy(fixture('probesynth').definition)
            definition['id'] = 'own'
            definition['resources']['trackControl'] = {'stateBytes': 8, 'stateAlignment': 4,
                                                       'marks': {'capture': 2, 'line': 1}}
            own = dataclasses.replace(fixture('probesynth'), id='own', root=root, definition=definition)
            marks = ('<g id="own_marks">'
                     '<circle id="own_capture_0" cx="8" cy="8" r="4" fill="#30d158" stroke="#30d158" stroke-width="1"/>'
                     '<circle id="own_capture_1" cx="472" cy="8" r="4" fill="#30d158" stroke="#30d158" stroke-width="1"/>'
                     '<path id="own_line" d="M0 219 L479 219" fill="none" stroke="#1f6f3a" stroke-width="1"/>'
                     '</g>')
            # The IDs are exactly the declared ones.
            (root / 'marks.svg.fragment').write_text(marks, encoding='utf-8')
            with self.assertRaises(ValueError):
                native.validate_marks(own)
            (root / 'marks.svg.fragment').write_text(marks.replace('own_line', 'own_line_0'), encoding='utf-8')
            artwork = native.control_artwork([plain, own]).decode('utf-8')
            for bad in ('<g id="own_marks"><text id="own_capture_0" x="0" y="9" fill="#ffffff" font-family="a" '
                        'font-size="9px">x</text><circle id="own_capture_1" cx="8" cy="8" r="4" fill="#30d158" '
                        'stroke="#30d158" stroke-width="1"/><path id="own_line_0" d="M0 0 L9 9" fill="none" '
                        'stroke="#1f6f3a" stroke-width="1"/></g>',
                        marks.replace('own_marks', 'marks').replace('own_line', 'own_line_0')):
                (root / 'marks.svg.fragment').write_text(bad, encoding='utf-8')
                with self.subTest(bad=bad), self.assertRaises(ValueError):
                    native.validate_marks(own)
            overlay = generate_track_control_overlay(folder, [plain, own]).read_text(encoding='utf-8')
        # The SDK's marks first, then the machine's own, with each machine's nodes.
        self.assertEqual(artwork.count('</svg>'), 1)
        self.assertLess(artwork.index('overlay_line'), artwork.index('own_capture_0'))
        self.assertIn('const u32 kNodes = 8;', overlay)
        self.assertIn('"own_capture_0",\n    "own_capture_1",\n    "own_line_0",', overlay)
        self.assertIn('{0, 4, 4, 1},  // probesynth', overlay)
        self.assertIn('{5, 2, 7, 1},  // own', overlay)

    def test_builds_without_track_control_stay_as_before(self):
        from tooling.firmware import active_services, patch_active
        plain = [fixture(name) for name in ('probesynth', 'probefx')]
        steering = copy.deepcopy(fixture('probesynth'))
        steering.definition['resources']['trackControl'] = {'stateBytes': 8, 'stateAlignment': 4}
        hooks = [p for p in config()['patches'] if p.get('service') == 'control']
        self.assertEqual(sorted(p['symbol'] for p in hooks),
                         ['sdk_gui_tick', 'sdk_param_edit_set', 'sdk_param_shown_hook', 'sdk_param_step_set',
                          'sdk_project_clear', 'sdk_project_clear_random'])
        for machines, used in ((plain, False), ([steering], True)):
            services = active_services(config(), native.service_users(machines))
            self.assertEqual('control' in services, used)
            self.assertEqual(all(patch_active(p, services) for p in hooks), used)
            units = {step for _name, _m, step in native.plan(machines)}
            for unit in ('track_control', 'track_control_table', 'track_control_hooks', 'track_control_overlay',
                         'track_control_project'):
                self.assertEqual(unit in units, used)
        # Startup reaches the saved data's writer only with the service.
        from tooling.startup import render
        plain_startup, control_startup = render(['probe']), render(['probe'], control=True)
        self.assertNotIn('sdk_overlay_startup', plain_startup)
        self.assertIn('u32 sdk_project_kept(void*) { return 0; }', plain_startup)
        self.assertNotIn('sdk_control_kept_write', plain_startup)
        self.assertIn('    sdk_overlay_startup();', control_startup)
        self.assertIn('{ return sdk_control_kept_write(file); }', control_startup)

    def test_gui_tick_is_a_reviewed_call_in_a_shared_unit(self):
        hooks = [p for p in config()['patches']
                 if p.get('symbol') in ('sdk_gui_tick', 'sdk_param_step_set', 'sdk_param_edit_set', 'sdk_param_shown_hook')]
        self.assertEqual([(int(p['address'], 16), p['length'], p['call']) for p in hooks],
                         [(0x8c0baade, 8, True), (0x8d58c5d8, 4, True), (0x8d5ad5ae, 4, True), (0x8d5aca48, 4, False)])
        self.assertTrue(all(p.get('service') == 'control' for p in hooks))
        # CC = R0 == 0; IF CC JUMP 8d5aca80: the getter's choice of live or base value.
        self.assertEqual(hooks[3]['before'], '000c1b18')
        for unit in ('track_control', 'track_control_hooks'):
            self.assertIn(unit, native.SDK_UNITS)
            self.assertEqual(native.UNIT_SERVICES[unit], 'control')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_real_platform_adapter_overlays_and_restores(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        with tempfile.TemporaryDirectory(prefix='track-control-') as directory:
            folder = Path(directory)
            # Substitute only the stock-address boundary, as other SDK fixtures do.
            stock = (platform / 'stock.hpp').read_text(encoding='utf-8')
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock, encoding='utf-8')
            (folder / 'track_control.cpp').write_bytes((platform / 'track_control.cpp').read_bytes())
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                str(ROOT / 'tests/fixtures/track_control.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_hooked_calls_reach_the_original_functions(self):
        from tooling.firmware import absolute_target, decode_branch
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        profile = config()
        self.assertEqual(sha(stock), profile['stock_sha256'])
        patch = next(p for p in profile['patches'] if p.get('symbol') == 'sdk_gui_tick')
        offset = mapped_offset(ldr(stock), int(patch['address'], 16), patch['length'])
        self.assertEqual(offset, int(patch['file_offset'], 16))
        self.assertEqual(absolute_target(stock[offset:offset + 8]), 0x8d580074)
        patch = next(p for p in profile['patches'] if p.get('symbol') == 'sdk_param_step_set')
        address = int(patch['address'], 16)
        offset = mapped_offset(ldr(stock), address, patch['length'])
        self.assertEqual(offset, int(patch['file_offset'], 16))
        self.assertEqual(decode_branch(address, stock[offset:offset + 4]), (0x8d5acea8, True))
        patch = next(p for p in profile['patches'] if p.get('symbol') == 'sdk_param_edit_set')
        address = int(patch['address'], 16)
        offset = mapped_offset(ldr(stock), address, patch['length'])
        self.assertEqual(offset, int(patch['file_offset'], 16))
        self.assertEqual(decode_branch(address, stock[offset:offset + 4]), (0x8d5acf28, True))
        patch = next(p for p in profile['patches'] if p.get('symbol') == 'sdk_param_shown_hook')
        offset = mapped_offset(ldr(stock), int(patch['address'], 16), patch['length'])
        self.assertEqual(offset, int(patch['file_offset'], 16))
        self.assertEqual(stock[offset:offset + 4].hex(), patch['before'])


if __name__ == '__main__':
    unittest.main()
