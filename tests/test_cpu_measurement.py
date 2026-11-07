import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from tooling import catalog, cpu_measurement
from tooling.benchmark import cases
from tooling.profile_report import aggregate, create, package_data
from tooling.formats import sha
from machine_fixtures import fixture
from test_catalog import PUBLISHED, document


def measurement(kind='synth', commit=None):
    value = dict(benchmark=cpu_measurement.BENCHMARKS[kind], commit=commit or PUBLISHED['commit'],
                 firmware_sha256='a' * 64, runs=3, block_percent=4.81, peak_percent=6.0,
                 report='https://github.com/someone/machine/issues/1#issuecomment-123')
    if kind == 'synth':
        value.update(midpoint_percent=3.2, combined_percent=4.5,
                     worst_setting=dict(control=1, label='Tone', value=0x7fffffff))
    if kind == 'drum':
        value.update(peak_percent=55.0, midpoint_percent=3.2, kit_percent=21.0, choke_percent=6.1, ceiling_percent=44.0,
                     worst_model=dict(run='voice', voices=1, model=7, level=127))
    return value


def legacy_synth(commit=None):
    value = measurement('synth', commit)
    del value['midpoint_percent'], value['combined_percent'], value['worst_setting'], value['peak_percent']
    return dict(value, benchmark='opxy-synth-c8-v1')


def frame(average, peak=None):
    if peak is None:
        peak = min(65535, average * 2)
    return dict(command=0x94, route=0x12, payload_hex=(struct.pack('>HHH', peak, average, 1451) * 20).hex())


class Measurements(unittest.TestCase):
    def test_peak_required_for_fresh_records_and_legacy_transition_policy(self):
        for kind in cpu_measurement.BENCHMARKS:
            entry = dict(PUBLISHED, kind=kind, cpu=measurement(kind))
            old_cpu = {k: v for k, v in entry['cpu'].items() if k != 'peak_percent'}
            old = dict(entry, cpu=old_cpu)
            catalog.validate(document(old))
            catalog.changes([old], [old])
            catalog.changes([dict(old, status='draft')], [old])
            catalog.changes([old], [dict(old, cpu=dict(old_cpu, report='https://example.com/report'))])
            catalog.changes([old], [entry])  # offline reprocessing adds the real peak
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, 'worst observed block peak'):
                catalog.changes([], [old])
            changed = dict(old, commit='b'*40, tag='v2', cpu=dict(old_cpu, commit='b'*40))
            with self.assertRaisesRegex(ValueError, 'worst observed block peak'):
                catalog.changes([old], [changed])
            with self.assertRaisesRegex(ValueError, 'worst observed block peak'):
                catalog.changes([entry], [old])  # cannot strip peaks from an existing record
            catalog.changes([old], [dict(old, commit='b'*40, tag='v2')], allow_carried_cpu=True)
            for peak in (None, True, 0, -1, 100, float('nan'), float('inf'), '5', 4.0):
                with self.subTest(kind=kind, peak=peak), self.assertRaises(ValueError):
                    catalog.validate(document(dict(entry, cpu=dict(entry['cpu'], peak_percent=peak))))
        with self.assertRaisesRegex(ValueError, 'whole-kit'):
            cpu_measurement.validate(dict(measurement('drum'), peak_percent=40), 'drum')

    def test_peak_keeps_one_off_spike_in_a_different_run_from_heaviest_mean(self):
        from dataclasses import replace
        base = fixture('probesynth')
        drum = replace(base, kind='drum', id='probedrum', definition=dict(base.definition,
                       resources=dict(base.definition['resources'], modelCount=2, voiceCapacity=8)))
        machines = [base, fixture('probefx'), fixture('probesample'), drum]
        expected = cases(machines, sweep=True)
        identity = dict(firmware_sha256='a'*64)
        captures = [dict(mode='benchmark', package=identity, expected_cases=expected, complete=True,
                         errors=[], collector_sha256='d'*64, frames=[frame(1000) for _ in expected],
                         utc=str(i)) for i in range(3)]
        for machine in machines:
            indices = [i for i, c in enumerate(expected) if c['name'] == machine.id]
            # High mean in the standard/single-voice run, but one short spike elsewhere.
            mean_index = indices[1] if machine.kind == 'drum' else indices[0]
            for capture in captures:
                capture['frames'][mean_index] = frame(3000, 4000)
            spike_index = indices[-1]  # combined synth or whole-kit ceiling for drums
            captures[0]['frames'][spike_index] = frame(1000, 12000)
            result, _, _, runs, _, _ = aggregate(captures, expected, identity, machine.id, 'e'*40, None)
            self.assertAlmostEqual(result['block_percent'], 300000/65535, places=5)
            self.assertAlmostEqual(result['peak_percent'], 1200000/65535, places=5)
            self.assertEqual(max(runs, key=lambda r: r[2])[0], expected[spike_index])
            # The peak must survive adding quieter captures and reordering them.
            reverse = aggregate(list(reversed(captures)), expected, identity, machine.id, 'e'*40, None)[0]
            self.assertEqual(reverse['peak_percent'], result['peak_percent'])
            for average, peak, message in ((1000, 65535, 'Clipped peak'), (1000, 0, 'below the average'),
                                            (1000, 999, 'below the average'), (0, 2000, 'Peak without')):
                broken = copy.deepcopy(captures)
                broken[0]['frames'][spike_index] = frame(average, peak)
                with self.subTest(kind=machine.kind, peak=peak), self.assertRaises(ValueError):
                    aggregate(broken, expected, identity, machine.id, 'e'*40, None)

    def test_report_binds_clean_committed_sources_and_omits_device_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            machine_root = root / 'machine'
            machine_root.mkdir()
            catalog.git('init', cwd=machine_root)
            source = machine_root / 'engine.cpp'
            source.write_text('// committed machine source\n')
            catalog.git('add', 'engine.cpp', cwd=machine_root)
            catalog.git('-c', 'user.name=Test', '-c', 'user.email=test@example.com',
                        '-c', 'commit.gpgsign=false', 'commit', '-m', 'Fixture', cwd=machine_root)
            commit = catalog.git('rev-parse', 'HEAD', cwd=machine_root).strip()
            machine = SimpleNamespace(id='probesynth', root=machine_root,
                                      files=lambda: {'external/probesynth/engine.cpp': source})
            expected = cases([fixture('probesynth')], sweep=True)
            package = root / 'package'
            (package / 'candidate').mkdir(parents=True)
            (package / 'analysis').mkdir()
            # No firmware fixture is needed to exercise the offline report contract.
            firmware = b'non-firmware test placeholder'
            (package / 'candidate/os.ldr').write_bytes(firmware)
            report = dict(profiling=True, benchmark_cases=expected,
                          sources={'external/probesynth/engine.cpp': sha(source.read_bytes())})
            report_bytes = json.dumps(report).encode()
            (package / 'analysis/compile-report.json').write_bytes(report_bytes)
            (package / 'manifest.json').write_text(json.dumps(dict(profiling=True, files={
                'candidate/os.ldr': sha(firmware), 'analysis/compile-report.json': sha(report_bytes)})))
            _, _, identity = package_data(package)
            frames = [dict(command=0x94, route=0x12,
                           payload_hex=(struct.pack('>HHH', 3000, 1000, 1451)*20).hex()) for _ in expected]
            paths = []
            for index in range(3):
                path = root / f'run-{index}.json'
                path.write_text(json.dumps(dict(mode='benchmark', package=identity, expected_cases=expected,
                    complete=True, errors=[], collector_sha256='d'*64, frames=frames, utc=str(index),
                    identity={'serial': 'PRIVATE-SERIAL'})))
                paths.append(path)
            with patch('tooling.native.package_collection', return_value=[]), \
                 patch('tooling.native.package_layout', return_value=None), \
                 patch('tooling.native.configuration', return_value=(None, [machine], None)):
                output = create(package, paths, 'probesynth', measurement()['report'], root / 'report')
                published = json.loads((output / 'catalog-cpu.json').read_text())['cpu']
                self.assertEqual(published['commit'], commit)
                self.assertEqual(published['benchmark'], 'opxy-synth-c8-sweep-v1')
                self.assertAlmostEqual(published['peak_percent'], 300000 / 65535, places=5)
                self.assertIn('| midpoint (stock) |', (output / 'report.md').read_text())
                self.assertIn('Worst observed block: **4.578%**', (output / 'report.md').read_text())
                self.assertNotIn('PRIVATE-SERIAL', (output / 'report.md').read_text())
                source.write_text('// changed after measurement\n')
                with self.assertRaisesRegex(ValueError, 'Commit the machine'):
                    create(package, paths, 'probesynth', measurement()['report'], root / 'dirty')
                catalog.git('add', 'engine.cpp', cwd=machine_root)
                catalog.git('-c', 'user.name=Test', '-c', 'user.email=test@example.com',
                            '-c', 'commit.gpgsign=false', 'commit', '-m', 'Changed', cwd=machine_root)
                with self.assertRaisesRegex(ValueError, 'differs from the measured build'):
                    create(package, paths, 'probesynth', measurement()['report'], root / 'stale')

    def test_all_kinds_and_stale_or_invalid_measurements(self):
        for kind in cpu_measurement.BENCHMARKS:
            entry = dict(PUBLISHED, kind=kind, cpu=measurement(kind))
            catalog.validate(document(entry))
            # The evidence link is optional.
            unlinked = dict(entry['cpu']); del unlinked['report']
            catalog.validate(document(dict(entry, cpu=unlinked)))
            # One capture is enough; a hundred is the most.
            for runs in (1, 100):
                catalog.validate(document(dict(entry, cpu=dict(entry['cpu'], runs=runs))))
            for change in ({'commit': 'b'}, {'runs': True}, {'runs': 0}, {'runs': 101}, {'block_percent': float('nan')},
                           {'block_percent': 0}, {'block_percent': 100}, {'block_percent': True},
                           {'report': 'javascript:alert(1)'}, {'report': 'https://user:pass@example.com/x'},
                           {'firmware_sha256': 'x'}, {'benchmark': 'host-estimate'}, {'benchmark': []}):
                with self.subTest(kind=kind, change=change), self.assertRaises(ValueError):
                    catalog.validate(document(dict(entry, cpu=dict(entry['cpu'], **change))))
        # A synth's sweep record names its midpoint run and heaviest setting.
        entry = dict(PUBLISHED, kind='synth', cpu=measurement('synth'))
        for change in ({'midpoint_percent': 5.0}, {'midpoint_percent': 0}, {'combined_percent': 5.0},
                       {'combined_percent': True}, {'worst_setting': None},
                       {'worst_setting': dict(control=8, label='Tone', value=0)},
                       {'worst_setting': dict(control=1, label='', value=0)},
                       {'worst_setting': dict(control=1, label='Tone', value=-1)},
                       {'worst_setting': dict(control=1, label='Tone')}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                catalog.validate(document(dict(entry, cpu=dict(entry['cpu'], **change))))
        # A list control's heaviest setting keeps its option name.
        listed = dict(entry['cpu'], worst_setting=dict(control=1, label='Octave', value=7, option='+2'))
        catalog.validate(document(dict(entry, cpu=listed)))
        self.assertIn('Octave +2', cpu_measurement.above_recommended(listed))
        for option in ('', 7, 'x' * 33):
            with self.subTest(option=option), self.assertRaises(ValueError):
                catalog.validate(document(dict(entry, cpu=dict(listed, worst_setting=dict(listed['worst_setting'], option=option)))))
        # Records from before the sweep stay valid in the catalog.
        catalog.validate(document(dict(entry, cpu=legacy_synth())))
        for kind in ('effect', 'sample'):
            with self.assertRaises(ValueError):
                catalog.validate(document(dict(PUBLISHED, kind=kind, cpu=dict(measurement(kind), midpoint_percent=1.0))))

    def test_new_synth_measurements_are_sweeps_and_heavy_ones_warn(self):
        entry = dict(PUBLISHED, kind='synth')
        with self.assertRaisesRegex(ValueError, 'control sweep'):
            catalog.changes([entry], [dict(entry, cpu=legacy_synth())])
        legacy = dict(entry, cpu=legacy_synth())
        catalog.changes([legacy], [legacy])  # unchanged, it stays
        # A cpu field that is not a record fails cleanly.
        for cpu in ('3.2', [], 3.2):
            with self.subTest(cpu=cpu), self.assertRaises(ValueError):
                catalog.changes([legacy], [dict(legacy, cpu=cpu)])
        # Linking its evidence later is not a new measurement.
        unlinked = dict(legacy, cpu={k: v for k, v in legacy['cpu'].items() if k != 'report'})
        catalog.changes([unlinked], [legacy])
        # Publishing a draft at the same commit keeps its older record.
        catalog.changes([dict(legacy, status='draft')], [legacy])
        # A maintainer may let an update carry the older record over.
        catalog.changes([legacy], [dict(legacy, tag='v2', commit='b'*40)], allow_carried_cpu=True)
        warning = cpu_measurement.above_recommended(measurement('synth'))
        self.assertIn('4.810%', warning)
        self.assertIn('Tone 100.0%', warning)
        self.assertIn('recommended 3.5%', warning)
        self.assertIsNone(cpu_measurement.above_recommended(dict(measurement('synth'), block_percent=3.5, peak_percent=3.5)))
        peak_only = cpu_measurement.above_recommended(dict(measurement('synth'), block_percent=3.0, peak_percent=4.5))
        self.assertIn('Worst observed block peak 4.500%', peak_only)
        self.assertNotIn('Mean', peak_only)
        self.assertIn('Mean 4.810%', warning)
        self.assertIn('Worst observed block peak 6.000%', warning)
        # Both recommendations warn; neither 3.5% nor 4.5% is an admission ceiling.
        cpu_measurement.validate(measurement('synth'), 'synth', legacy=False)
        self.assertIn('above the recommended', cpu_measurement.above_recommended(legacy_synth()))

    def test_the_cpu_high_label_follows_the_peak_or_else_the_average(self):
        # The reported peak decides when there is one, even under a heavier average.
        self.assertEqual(cpu_measurement.worst_case(dict(measurement('synth'), block_percent=4.8, peak_percent=4.4)), (4.4, 'peak'))
        self.assertIsNone(cpu_measurement.high(dict(measurement('synth'), block_percent=4.8, peak_percent=4.4)))
        labelled = cpu_measurement.high(dict(measurement('synth'), block_percent=3.0, peak_percent=4.6))
        self.assertIn('worst observed block peak 4.600%', labelled)
        # A record without a peak falls back to its average, and says so.
        self.assertEqual(cpu_measurement.worst_case(legacy_synth()), (legacy_synth()['block_percent'], 'average'))
        average = cpu_measurement.high(dict(legacy_synth(), block_percent=5.25))
        self.assertIn('average (no peak reported) 5.250%', average)
        # A drum kit, by its worst single sound: its whole-kit peak is ignored, and its
        # heaviest single voice's average decides, with or without a peak.
        drum = dict(measurement('drum'), block_percent=2.7, peak_percent=48.2)
        self.assertEqual(cpu_measurement.worst_case(drum), (2.7, 'average'))
        self.assertIsNone(cpu_measurement.high(drum))
        for drum in (dict(measurement('drum'), block_percent=5.0),
                     {k: v for k, v in dict(measurement('drum'), block_percent=5.0).items() if k != 'peak_percent'}):
            self.assertIn('worst single sound average (no single-sound peak recorded) 5.000%', cpu_measurement.high(drum))
        # Its single-voice peak, when recorded, decides instead.
        drum = dict(measurement('drum'), block_percent=2.7, peak_percent=48.2, voice_peak_percent=4.4)
        self.assertEqual(cpu_measurement.worst_case(drum), (4.4, 'peak'))
        self.assertIsNone(cpu_measurement.high(drum))
        drum['voice_peak_percent'] = 7.25
        self.assertIn('worst single sound peak 7.250%', cpu_measurement.high(drum))
        # Strictly above the threshold.
        at = cpu_measurement.HIGH_PERCENT
        self.assertIsNone(cpu_measurement.high(dict(measurement('synth'), block_percent=at, peak_percent=at)))
        self.assertIsNone(cpu_measurement.high(dict(legacy_synth(), block_percent=at)))

    def test_changed_submissions_require_a_measurement_but_legacy_entries_can_stay(self):
        for kind in cpu_measurement.BENCHMARKS:
            entry = dict(PUBLISHED, kind=kind)
            catalog.changes([entry], [entry])
            for before, after in (([], [entry]), ([entry], [dict(entry, tag='v2')])):
                with self.assertRaisesRegex(ValueError, 'require hardware'):
                    catalog.changes(before, after)
            measured = dict(entry, cpu=measurement(kind))
            catalog.changes([entry], [measured])
            # An update remeasures, unless a catalog maintainer lets it carry its previous
            # measurement over unchanged, naming the commit it measured.
            carried = dict(measured, tag='v2', commit='b'*40)
            with self.assertRaisesRegex(ValueError, 'remeasure when updating'):
                catalog.changes([measured], [carried])
            catalog.changes([measured], [carried], allow_carried_cpu=True)
            catalog.changes([measured], [dict(carried, cpu=measurement(kind, commit='b'*40))])
            for before, after in (
                    ([], [dict(entry, cpu=measurement(kind, commit='b'*40))]),
                    ([entry], [dict(entry, cpu=measurement(kind, commit='b'*40))]),
                    ([measured], [dict(measured, commit='b'*40, cpu=dict(measured['cpu'], block_percent=5.5))]),
                    ([measured], [dict(measured, commit='b'*40, cpu=measurement(kind, commit='c'*40))])):
                with self.subTest(kind=kind, after=after), self.assertRaisesRegex(ValueError, 'pinned commit'):
                    catalog.changes(before, after, allow_carried_cpu=True)
            catalog.changes([entry], [dict(entry, status='retired')])

    def test_report_rejects_repeated_wrong_build_incomplete_or_inactive_runs(self):
        expected = cases([fixture('probesynth'), fixture('probefx'), fixture('probesample')], sweep=True)
        identity = dict(firmware_sha256='a'*64, manifest_sha256='b'*64, compile_report_sha256='c'*64)
        frames = [dict(command=0x94, route=0x12,
                       payload_hex=(struct.pack('>HHH', 3000, 1000, 1451)*20).hex()) for _ in expected]
        captures = [dict(mode='benchmark', package=identity, expected_cases=expected, complete=True,
                         errors=[], collector_sha256='d'*64, frames=frames, utc=str(i)) for i in range(3)]
        for name, kind in (('probesynth', 'synth'), ('probefx', 'effect'), ('probesample', 'sample')):
            result, values, case, _runs, _combinations, _silent = aggregate(captures, expected, identity, name, 'e'*40,
                                                                   measurement()['report'])
            self.assertEqual(result['benchmark'], cpu_measurement.BENCHMARKS[kind])
            self.assertAlmostEqual(result['block_percent'], 100000/65535, places=5)
        # One capture is enough; a hundred is the most.
        single = aggregate(captures[:1], expected, identity, 'probesynth', 'e'*40, None)[0]
        self.assertEqual(single['runs'], 1)
        many = [dict(captures[0], utc=str(i)) for i in range(101)]
        self.assertEqual(aggregate(many[:100], expected, identity, 'probesynth', 'e'*40, None)[0]['runs'], 100)
        with self.assertRaisesRegex(ValueError, '1 to 100'):
            aggregate(many, expected, identity, 'probesynth', 'e'*40, None)
        # Without an evidence URL the record has no report link.
        self.assertNotIn('report', aggregate(captures, expected, identity, 'probesynth', 'e'*40, None)[0])
        broken = [[], [captures[0]]*3,
                  [dict(captures[0], package={}), *captures[1:]],
                  [dict(captures[0], errors=['CRC']), *captures[1:]],
                  [dict(captures[0], complete=False), *captures[1:]],
                  [dict(captures[0], frames=frames[:-1]), *captures[1:]]]
        silent = copy.deepcopy(captures)
        silent[0]['frames'][-1]['payload_hex'] = '00' * 120
        broken.append(silent)
        # A single capture is refused for the same faults.
        broken += [[dict(captures[0], package={})], [dict(captures[0], errors=['CRC'])],
                   [dict(captures[0], complete=False)], [dict(captures[0], frames=frames[:-1])], silent[:1]]
        for data in broken:
            with self.assertRaises(ValueError):
                aggregate(data, expected, identity, 'probesample', 'e'*40, measurement()['report'])

    def test_a_drum_kit_publishes_its_heaviest_voice_beside_the_whole_kit(self):
        from dataclasses import replace
        base = fixture('probesynth')
        definition = dict(base.definition, resources=dict(base.definition['resources'], modelCount=2, voiceCapacity=8))
        drum = replace(base, kind='drum', id='probedrum', definition=definition)
        expected = cases([drum])
        own = [i for i, c in enumerate(expected) if c['source'] == 'sdk']
        self.assertEqual(len(own), 1 + 2 * 3 + 2)
        identity = dict(firmware_sha256='a'*64, manifest_sha256='b'*64, compile_report_sha256='c'*64)
        averages = [1000] * len(expected)
        averages[own[0]] = 5000     # the kit's own keys, whole kits are above any one voice
        averages[own[2]] = 1200     # model 0 at 0
        averages[own[6]] = 2000     # model 1 at 127, the heaviest voice
        averages[own[4]] = 1500     # model 1 at 64, the heaviest at the middle
        averages[own[-2]] = 3000    # the choke
        averages[own[-1]] = 16000   # the ceiling
        capture = dict(mode='benchmark', package=identity, expected_cases=expected, complete=True, errors=[],
                       collector_sha256='d'*64, frames=[frame(a) for a in averages], utc='0')
        result, values, case, runs, _combinations, silent = aggregate([capture], expected, identity, 'probedrum', 'e'*40, None)
        self.assertEqual(result['benchmark'], 'opxy-drum-callback-v1')
        self.assertAlmostEqual(result['block_percent'], 200000/65535, places=5)
        self.assertAlmostEqual(result['midpoint_percent'], 150000/65535, places=5)
        self.assertAlmostEqual(result['kit_percent'], 500000/65535, places=5)
        self.assertAlmostEqual(result['choke_percent'], 300000/65535, places=5)
        self.assertAlmostEqual(result['ceiling_percent'], 1600000/65535, places=5)
        self.assertEqual(result['worst_model'], dict(run='voice', voices=1, model=1, level=127))
        # frame() doubles each average for its peak: the single-voice runs' worst is model 1 at 127.
        self.assertAlmostEqual(result['voice_peak_percent'], 400000/65535, places=5)
        self.assertEqual((len(values), len(runs), silent), (1, len(own), []))
        cpu_measurement.validate(result, 'drum', 'e'*40)
        # A saturated run names itself, and a heavy voice warns as a synth's does.
        averages[own[-1]] = 65535
        with self.assertRaisesRegex(ValueError, 'the heaviest model, 8 voices'):
            aggregate([dict(capture, frames=[frame(a) for a in averages])], expected, identity, 'probedrum', 'e'*40, None)
        self.assertIn('model 7 at 127', cpu_measurement.above_recommended(measurement('drum')))
        for change in ({'worst_model': dict(run='voice', voices=1, model=64, level=127)},
                       {'worst_model': dict(run='voice', voices=1, model=1, level=5)},
                       {'kit_percent': 0}, {'midpoint_percent': 9.0},
                       # The single-voice peak lies between the heaviest voice's average and the peak.
                       {'voice_peak_percent': 4.0}, {'voice_peak_percent': 56.0}, {'voice_peak_percent': '6'},
                       {'voice_peak_percent': float('nan')}, {'voice_peak_percent': True}):
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, 'cpu:'):
                cpu_measurement.validate(dict(measurement('drum'), **change), 'drum')

    def test_a_kits_single_voice_peak_leaves_out_its_whole_kit_runs(self):
        # Synthesized in the shape of a real kit's captures: three single-voice runs per
        # model, then the kit's own keys, the choke and the ceiling; two captures.
        from dataclasses import replace
        base = fixture('probesynth')
        definition = dict(base.definition, resources=dict(base.definition['resources'], modelCount=3, voiceCapacity=8))
        drum = replace(base, kind='drum', id='probedrum', definition=definition)
        expected = cases([drum])
        own = [i for i, c in enumerate(expected) if c['source'] == 'sdk']
        voices = [i for i in own if expected[i]['drum']['run'] == 'voice']
        whole = [i for i in own if expected[i]['drum']['run'] != 'voice']
        self.assertEqual((len(voices), len(whole)), (9, 3))
        identity = dict(firmware_sha256='a'*64)
        frames = [[frame(1000, 1500) for _ in expected] for _ in range(2)]
        for index in whole:              # whole kits: heavier on average and in spikes
            for capture in frames:
                capture[index] = frame(6000, 30000)
        frames[0][voices[4]] = frame(1200, 5200)   # one capture's spike on one sound
        frames[1][voices[7]] = frame(2500, 3000)   # the heaviest sound on average
        captures = [dict(mode='benchmark', package=identity, expected_cases=expected, complete=True, errors=[],
                         collector_sha256='d'*64, frames=f, utc=str(n)) for n, f in enumerate(frames)]
        result = aggregate(captures, expected, identity, 'probedrum', 'e'*40, None)[0]
        self.assertAlmostEqual(result['voice_peak_percent'], 520000/65535, places=5)
        self.assertAlmostEqual(result['peak_percent'], 3000000/65535, places=5)
        self.assertAlmostEqual(result['block_percent'], 175000/65535, places=5)  # median of 1000 and 2500
        cpu_measurement.validate(result, 'drum', 'e'*40, legacy=False)
        self.assertIn('worst single sound peak 7.935%', cpu_measurement.high(result))
        # Optional: an older kit record without it stays valid, and it is a kit's field only.
        cpu_measurement.validate({k: v for k, v in result.items() if k != 'voice_peak_percent'}, 'drum')
        with self.assertRaisesRegex(ValueError, 'unexpected measurement fields'):
            cpu_measurement.validate(dict(measurement('synth'), voice_peak_percent=6.0), 'synth')
        with self.assertRaisesRegex(ValueError, 'voice_peak_percent'):
            cpu_measurement.validate({k: v for k, v in dict(measurement('drum'), voice_peak_percent=5.0).items()
                                      if k != 'peak_percent'}, 'drum')

    def test_a_sweep_publishes_its_heaviest_run_beside_the_midpoint(self):
        expected = cases([fixture('probesynth')], sweep=True)
        own = [i for i, c in enumerate(expected) if c['source'] == 'sdk']
        identity = dict(firmware_sha256='a'*64, manifest_sha256='b'*64, compile_report_sha256='c'*64)
        averages = [1000] * len(expected)
        averages[own[0]] = 2000           # the midpoint run
        averages[own[3]] = 3000           # the heaviest swept setting
        averages[own[-1]] = 2500          # every control at its heaviest position
        captures = [dict(mode='benchmark', package=identity, expected_cases=expected, complete=True, errors=[],
                         collector_sha256='d'*64, frames=[frame(a) for a in averages], utc=str(i)) for i in range(3)]
        result, _values, _case, runs, combinations, _silent = aggregate(captures, expected, identity, 'probesynth', 'e'*40, None)
        self.assertAlmostEqual(result['block_percent'], 300000/65535, places=5)
        self.assertAlmostEqual(result['midpoint_percent'], 200000/65535, places=5)
        self.assertAlmostEqual(result['combined_percent'], 250000/65535, places=5)
        self.assertEqual(result['worst_setting'], expected[own[3]]['setting'])
        self.assertEqual(len(runs), len(own))
        # Each capture's combined run took the swept setting that ran heaviest for its control.
        self.assertIn(expected[own[3]]['setting'], combinations[0])
        self.assertEqual(len(combinations), 3)
        # Heaviest together: the record is the combined run, and the warning says so.
        averages[own[-1]] = 4000
        captures = [dict(c, frames=[frame(a) for a in averages]) for c in captures]
        result = aggregate(captures, expected, identity, 'probesynth', 'e'*40, None)[0]
        self.assertEqual(result['block_percent'], result['combined_percent'])
        self.assertIn('every control at its heaviest position', cpu_measurement.above_recommended(result))
        averages[own[-1]] = 2500
        # Heaviest at the midpoint: the record is the midpoint run, still naming the heaviest swept one.
        averages[own[0]] = 4000
        captures = [dict(c, frames=[frame(a) for a in averages]) for c in captures]
        result = aggregate(captures, expected, identity, 'probesynth', 'e'*40, None)[0]
        self.assertEqual(result['block_percent'], result['midpoint_percent'])
        # A swept setting that silences the voice is left out, not a reason to refuse.
        silent = list(averages)
        silent[own[1]] = 0
        captures = [dict(c, frames=[frame(a) for a in silent]) for c in captures]
        result, _values, _case, runs, _combinations, silent_cases = aggregate(captures, expected, identity, 'probesynth', 'e'*40, None)
        self.assertIn((expected[own[1]], None, 0.0), runs)
        self.assertEqual(silent_cases, [expected[own[1]]])
        self.assertNotEqual(result['worst_setting'], expected[own[1]]['setting'])
        # One capture cannot tell a silenced voice from a failed run: it asks for a second.
        with self.assertRaisesRegex(ValueError, 'Collect a second capture'):
            aggregate(captures[:1], expected, identity, 'probesynth', 'e'*40, None)
        aggregate(captures[:2], expected, identity, 'probesynth', 'e'*40, None)
        # A note that ended early (little activity) still timed its blocks: its cost counts.
        short = [dict(command=0x94, route=0x12, payload_hex=(struct.pack('>HHH', 6000, 5000, 5) * 20).hex())
                 if i == own[2] else frame(a) for i, a in enumerate(averages)]
        brief = [dict(c, frames=short) for c in captures]
        result = aggregate(brief, expected, identity, 'probesynth', 'e'*40, None)[0]
        self.assertAlmostEqual(result['block_percent'], 500000/65535, places=5)
        self.assertEqual(result['worst_setting'], expected[own[2]]['setting'])
        # Silent in only one capture: that run failed, and the sweep is refused rather than understated.
        flaky = [dict(c, frames=[frame(a) for a in (silent if i == 0 else averages)]) for i, c in enumerate(captures)]
        with self.assertRaisesRegex(ValueError, 'some runs but not others'):
            aggregate(flaky, expected, identity, 'probesynth', 'e'*40, None)
        # A silent midpoint or combined run, or a saturated one, is still refused.
        for index, average in ((own[0], 0), (own[-1], 0), (own[2], 65535)):
            broken = list(averages)
            broken[index] = average
            captures = [dict(c, frames=[frame(a) for a in broken]) for c in captures]
            with self.subTest(index=index, average=average), self.assertRaises(ValueError):
                aggregate(captures, expected, identity, 'probesynth', 'e'*40, None)
        # A synth built without the sweep cannot publish.
        plain = cases([fixture('probesynth')])
        captures = [dict(c, expected_cases=plain, frames=[frame(1000)] * len(plain)) for c in captures]
        with self.assertRaisesRegex(ValueError, 'control sweep'):
            aggregate(captures, plain, identity, 'probesynth', 'e'*40, None)
