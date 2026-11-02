"""RAM captures must bind the loaded image as well as the resident firmware."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from machine_fixtures import fixture
from test_cpu_measurement import frame, measurement
from tooling import collection_profile, cpu_measurement, profile_report
from tooling.benchmark import cases


class CollectionProfile(unittest.TestCase):
    def identity(self):
        return dict(manifest_sha256='a'*64, compile_report_sha256='b'*64, firmware_sha256='c'*64,
                    collection_sha256='d'*64, kernel_record_sha256='e'*64)

    def capture(self, expected, identity):
        return dict(mode='benchmark', package=identity, expected_cases=expected, complete=True, errors=[],
                    collector_sha256='f'*64, frames=[frame(1000) for _ in expected])

    def test_static_firmware_and_ram_images_have_distinct_identities(self):
        expected = cases([fixture('probesynth')], sweep=True)
        identity = self.identity(); capture = self.capture(expected, identity)
        value = profile_report.aggregate([capture], expected, identity, 'probesynth', '1'*40, None)[0]
        self.assertEqual(value['firmware_sha256'], 'c'*64)
        self.assertEqual(value['collection_sha256'], 'd'*64)
        different = dict(identity, collection_sha256='9'*64)
        with self.assertRaisesRegex(ValueError, 'different build or workload'):
            profile_report.aggregate([capture], expected, different, 'probesynth', '1'*40, None)
        static = {k:v for k,v in identity.items() if k not in ('collection_sha256', 'kernel_record_sha256')}
        with self.assertRaisesRegex(ValueError, 'different build or workload'):
            profile_report.aggregate([capture], expected, static, 'probesynth', '1'*40, None)

    def test_drum_measurements_also_bind_the_collection(self):
        from dataclasses import replace
        machine = fixture('probesynth')
        drum = replace(machine, id='drum', kind='drum', definition=dict(machine.definition,
            resources=dict(machine.definition['resources'], modelCount=3, voiceCapacity=8)))
        expected = cases([drum], sweep=True); identity = self.identity()
        value = profile_report.aggregate([self.capture(expected, identity)], expected, identity, 'drum', '1'*40, None)[0]
        self.assertEqual(value['collection_sha256'], identity['collection_sha256'])

    def test_collection_hash_validation_is_strict_and_static_records_still_work(self):
        value = measurement('synth')
        cpu_measurement.validate(value, 'synth')
        value['collection_sha256'] = 'a'*64
        cpu_measurement.validate(value, 'synth')
        for invalid in ('', 'a'*63, 'g'*64, 'A'*64, 123, None):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, 'collection_sha256'):
                cpu_measurement.validate(dict(value, collection_sha256=invalid), 'synth')

    def test_ram_collection_without_an_explicit_sdk_is_refused_before_device_access(self):
        from tools import benchmark
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); package = root/'package'; package.mkdir()
            (package/'collection.json').write_text('{}')
            with patch('tooling.collection_profile.subprocess.run') as process, self.assertRaisesRegex(ValueError, 'requires --sdk'):
                benchmark.main(['benchmark', '--package', str(package), '--out', str(root/'capture'), '--installed'])
            process.assert_not_called()
            self.assertFalse((root/'capture').exists())

    def test_the_explicit_sdk_is_used_instead_of_a_path_from_the_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); sdk = root/'sdk'; (sdk/'tooling').mkdir(parents=True)
            (sdk/'tooling/collection_bundle.py').write_text('')
            package = root/'package'; package.mkdir()
            (package/'collection.json').write_text(json.dumps(dict(sdk='/untrusted/package/code')))
            result = dict(manifest={}, report={}, identity=self.identity())
            with patch('tooling.collection_profile.subprocess.run', return_value=subprocess.CompletedProcess([], 0, json.dumps(result), '')) as process:
                self.assertEqual(profile_report.package_data(package, source_sdk=sdk)[2], self.identity())
            command = process.call_args.args[0]
            self.assertEqual(command[command.index('--sdk')+1], str(sdk.resolve()))
            self.assertNotIn('/untrusted/package/code', command)

    def test_a_failed_source_check_cannot_create_a_public_report(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)/'report'
            with patch('tooling.profile_report.package_data', return_value=({}, {}, self.identity())), \
                 patch('tooling.collection_profile.run', side_effect=ValueError('Machine source differs')):
                with self.assertRaisesRegex(ValueError, 'source differs'):
                    profile_report.create(Path(directory), [], 'probesynth', None, out, source_sdk=Path(directory))
            self.assertFalse(out.exists())

    def test_ram_report_publishes_both_hashes_and_omits_private_metadata(self):
        expected = cases([fixture('probesynth')], sweep=True); identity = self.identity()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); path = root/'capture.json'
            capture = self.capture(expected, identity); capture['identity'] = dict(serial='PRIVATE-SERIAL')
            path.write_text(json.dumps(capture))
            with patch('tooling.profile_report.package_data', return_value=({}, dict(benchmark_cases=expected), identity)), \
                 patch('tooling.collection_profile.run', return_value=dict(commit='1'*40)):
                out = profile_report.create(root, [path], 'probesynth', None, root/'report', source_sdk=root)
            text = (out/'report.md').read_text(); value = json.loads((out/'catalog-cpu.json').read_text())['cpu']
            self.assertIn('c'*64, text); self.assertIn('d'*64, text); self.assertNotIn('PRIVATE-SERIAL', text)
            self.assertEqual(value['collection_sha256'], 'd'*64)
