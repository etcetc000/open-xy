"""Diagnostic collection generation, ABI forwarding and strict report attribution."""
from dataclasses import replace
import json
from pathlib import Path
from unittest.mock import patch
import struct
import tempfile
import unittest

from tooling.benchmark import cases, decode, generate
from machine_fixtures import fixture
from tooling.pipeline import config
from tooling.firmware import decode_branch


class Benchmark(unittest.TestCase):
    def test_wire_codec_matches_retrieved_device_requests(self):
        from tools import benchmark as transport
        transport.selftest()
        self.assertEqual(transport.frame(0x21, 5, 0).hex(), 'f000207621660002210205020c00f7')
        self.assertEqual(transport.frame(0x21, 9, 1).hex(), 'f000207621660004210109027300f7')
        self.assertEqual(transport.frame(0x21, 0xc8, 2).hex(), 'f000207621660804210248024900f7')

    def test_arbitrary_names_and_all_machine_kinds(self):
        synth = replace(fixture('probesynth'), id='unfamiliar', identity=dict(engine=46, page=91))
        selected = [fixture('probefx'), synth, fixture('probesample')]
        self.assertEqual([c['kind'] for c in cases(selected)[9:]], ['effect', 'synth', 'sample'])
        self.assertEqual([c['metric'] for c in cases(selected)[9:]], [7, 5, 3])
        self.assertEqual(cases(selected)[10]['name'], 'unfamiliar')
        with tempfile.TemporaryDirectory() as folder:
            source = generate(folder, selected).read_text(encoding='utf-8')
            self.assertIn('unfamiliar_ready', source)
            self.assertNotIn('probesynth', source)
            self.assertIn('probefx_benchmark', source)
            self.assertIn('probesample_benchmark', source)
            empty = generate(folder, []).read_text(encoding='utf-8')
            self.assertIn('{0, 0, 0, 0, 0}', empty)

    def test_a_sweep_runs_each_synth_control_through_its_positions_after_the_standard_run(self):
        from tooling.benchmark import SWEEP, sweep_settings
        from tooling.cpu import sweep
        synth, effect = fixture('probesynth'), fixture('probefx')
        parameters = synth.definition['parameters']
        settings = sweep_settings(synth)
        self.assertEqual(len(settings), sum(len(sweep(p)) for p in parameters))
        defaults = [round(p['default'] * 0x7fffffff) for p in parameters] + [0] * (8 - len(parameters))
        for control, label, value, _option in settings:
            self.assertEqual(label, parameters[control]['label'])
            self.assertIn(value, sweep(parameters[control]))
        plain, swept = cases([effect, synth]), cases([effect, synth], sweep=True)
        self.assertEqual(swept[:len(plain)], plain)
        extra = swept[len(plain):]
        # Each swept setting, then every control at its heaviest position.
        self.assertEqual([c['benchmark'] for c in extra], [SWEEP] * (len(settings) + 1))
        self.assertEqual([(c['setting']['control'], c['setting']['value']) for c in extra[:-1]],
                         [(control, value) for control, _label, value, _option in settings])
        self.assertTrue(extra[-1]['combined'] and 'setting' not in extra[-1])
        with tempfile.TemporaryDirectory() as folder:
            source = generate(folder, [effect, synth], sweep=True).read_text(encoding='utf-8')
            # The synth is machines[1]: a row per setting with its control and value, and
            # one row of its defaults; the effect sweeps nothing.
            def table(text, name):
                body = text.split(f'const {name}[] = {{\n', 1)[1].split('\n};', 1)[0]
                return [line.strip() for line in body.splitlines() if line.strip()][:-1]
            self.assertEqual(table(source, 'Setting settings'),
                             ['{1, %d, %d},' % (c, v) for c, _l, v, _o in settings])
            self.assertEqual(table(source, 'Defaults defaults'), ['{1, {%s}},' % ', '.join(map(str, defaults))])
            self.assertIn('sdk_benchmark_parameters = controls;', source)
            plain_source = generate(folder, [effect, synth]).read_text(encoding='utf-8')
            self.assertEqual((table(plain_source, 'Setting settings'), table(plain_source, 'Defaults defaults')), ([], []))

    def test_list_controls_name_their_option(self):
        from tooling.benchmark import position
        self.assertEqual(position(dict(control=1, label='Octave', value=7, option='+2')), 'Octave +2')
        self.assertEqual(position(dict(control=0, label='Tone', value=0x7fffffff)), 'Tone 100.0%')
        synth = fixture('probesynth')
        options = {p['label']: p['mapping']['labels'] for p in synth.definition['parameters'] if p['mapping']['kind'] == 'enum'}
        for case in cases([synth], sweep=True):
            setting = case.get('setting')
            if setting:
                self.assertEqual('option' in setting, setting['label'] in options)

    def test_the_combined_run_takes_each_controls_first_heaviest_position(self):
        from tooling.benchmark import combined_positions
        a0, a1, b0, b1 = (dict(control=c, label=l, value=v) for c, l, v in
                          ((0, 'A', 0), (0, 'A', 9), (1, 'B', 0), (1, 'B', 9)))
        self.assertEqual(combined_positions([(a0, 5), (a1, 7), (b0, 3), (b1, 3)]), [a1, b0])
        self.assertEqual(combined_positions([]), [])

    def test_hook_replaces_only_the_explicit_diagnostic_runner(self):
        patch = next(p for p in config()['patches'] if p.get('symbol') == 'sdk_benchmark_run')
        self.assertEqual(decode_branch(int(patch['address'], 16), bytes.fromhex(patch['before'])),
                         (0x8d57cbbc, True))
        self.assertEqual(patch['length'], 4)

    def test_reports_refuse_missing_extra_corrupt_or_wrong_endpoint_data(self):
        expected = cases([fixture('probesynth')])
        frame = dict(command=0x94, route=0x12, payload_hex=(struct.pack('>HHH', 65535, 1000, 300) * 20).hex())
        capture = dict(complete=True, errors=[], frames=[frame] * len(expected))
        result = decode(capture, expected)
        self.assertEqual(result[-1]['machine']['name'], 'probesynth')
        self.assertTrue(result[-1]['metrics'][3]['maximum_clipped'])
        self.assertAlmostEqual(result[-1]['metrics'][3]['average_budget_percent'], 100000 / 65535)
        for change in [dict(complete=False), dict(errors=['CRC']), dict(frames=capture['frames'][:-1]),
                       dict(frames=capture['frames'] + [frame]),
                       dict(frames=[dict(frame, route=0x15)] * len(expected)),
                       dict(frames=[dict(frame, payload_hex='00')] * len(expected))]:
            with self.assertRaises(ValueError):
                decode(dict(capture, **change), expected)

    def test_collector_rejects_disabled_or_missing_profiling_before_device_access(self):
        from tools import benchmark as transport
        with tempfile.TemporaryDirectory() as folder:
            package = Path(folder)
            for value in (False, None, 1, 'on'):
                (package / 'manifest.json').write_text(json.dumps(dict(profiling=value)))
                with patch('sys.argv', ['benchmark.py', 'benchmark', '--package', str(package),
                                        '--out', str(package / 'capture')]):
                    with self.assertRaisesRegex(ValueError, 'profiling-enabled'):
                        transport.main()
                self.assertFalse((package / 'capture').exists())

    def test_effect_and_sample_callback_workloads_execute_and_release_private_storage(self):
        import os, subprocess
        from tooling.benchmark import append_callback
        from test_sample_services import HOST_CXX, ROOT
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler')
        for name, source in [('probefx', 'fx.cpp'), ('probesample', 'sample.cpp')]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                folder = Path(folder)
                machine = fixture(name)
                generated = folder / 'generated_bench.cpp'
                generated.write_text('#include "' + (machine.root / source).as_posix() + '"\n')
                if machine.definition['resources'].get('padSettings'):
                    with generated.open('a') as output:
                        output.write(f'''opxy::PadSettings {machine.prefix}_sample_defaults() {{ return {machine.id}::sample_defaults(); }}
opxy::PadSettings {machine.prefix}_sample_assign(const opxy::SampleAssignment* input) {{ return {machine.id}::sample_assign(*input); }}
''')
                append_callback(generated, machine)
                binary = folder / ('test.exe' if os.name == 'nt' else 'test')
                command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-variable',
                           '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'),
                           '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                           '-DBENCHMARK=' + name + '_benchmark',
                           *(['-DSAMPLE_BENCHMARK'] if machine.kind == 'sample' else []),
                           str(ROOT / 'tests/fixtures/benchmark_callbacks.cpp'), '-o', str(binary)]
                hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
                result = subprocess.run(command, capture_output=True, text=True, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
