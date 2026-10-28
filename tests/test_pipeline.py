"""Public tests use synthetic containers; private integration fixtures are opt-in."""
import copy
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tooling.formats import elf32, header, ldr, mapped_offset, sha
from tooling import pipeline
from tooling.pipeline import ROOT, PROTECTED, check_patch_boundaries, child, config, fresh_output


class Formats(unittest.TestCase):
    def fixture(self):
        return header(4, 0x1000, 4, 0) + b'abcd' + header(0x8004, 0x1000, 0, 0)

    def test_stored_mapping(self):
        blocks = ldr(self.fixture())
        self.assertEqual(mapped_offset(blocks, 0x1001, 2), 17)
        with self.assertRaises(ValueError):
            mapped_offset(blocks, 0x1003, 2)

    def test_header_checksum(self):
        bad = bytearray(self.fixture())
        bad[4] ^= 1
        with self.assertRaises(ValueError):
            ldr(bad)

    def test_truncation_suffix_and_missing_final(self):
        for data in (self.fixture()[:-1], self.fixture() + b'\0', self.fixture()[:20], b''):
            with self.subTest(data_length=len(data)), self.assertRaises(ValueError):
                ldr(data)

    def test_fill_is_not_stored_and_overlap_rejected(self):
        data = header(0x104, 0x1000, 16, 0) + self.fixture()
        with self.assertRaises(ValueError):
            mapped_offset(ldr(data), 0x1000, 4)
        data = header(0x104, 0x2000, 16, 0) + self.fixture()
        with self.assertRaises(ValueError):
            mapped_offset(ldr(data), 0x2000, 4)

    def test_address_overflow(self):
        with self.assertRaises(ValueError):
            ldr(header(0x104, 0xfffffff0, 32, 0) + self.fixture())

    def test_malformed_elf_rejected(self):
        for data in (b'', b'\x7fELF\x01\x01' + bytes(60)):
            with self.assertRaises(ValueError):
                elf32(data)


class Services(unittest.TestCase):
    def test_profiling_policy_is_strict_and_independent_of_services(self):
        from tooling.firmware import patch_active
        profile = config()
        timing = [p for p in profile['patches'] if p.get('profiling') is False]
        self.assertEqual(len(timing), 10)
        for p in timing:
            self.assertFalse(patch_active(p, {'samples'}))
            self.assertTrue(patch_active(p, set(), False))
            tagged = dict(p, service='samples')
            self.assertFalse(patch_active(tagged, set(), False))
            self.assertTrue(patch_active(tagged, {'samples'}, False))
        for invalid in (None, 0, 1, 'off', 'on'):
            with self.subTest(value=invalid), self.assertRaises(ValueError):
                patch_active(timing[0], set(), invalid)
        for invalid in (None, 0, 1, True, 'off'):
            bad = copy.deepcopy(profile)
            next(p for p in bad['patches'] if 'profiling' in p)['profiling'] = invalid
            with self.subTest(value=invalid), self.assertRaises(ValueError):
                check_patch_boundaries(bad, len(bad['patches']))
        bad = copy.deepcopy(profile)
        bad['patches'][0]['profiling'] = False  # fixed edits may never be optional
        with self.assertRaises(ValueError):
            check_patch_boundaries(bad, len(bad['patches']))

    def test_only_machines_that_use_a_service_link_and_patch_it(self):
        from tooling import native
        from tooling.firmware import active_services
        from machine_fixtures import NAMES, fixture
        profile = config()
        self.assertEqual(active_services(profile, ['synth', 'effect']), set())
        self.assertEqual(active_services(profile, ['sample']), {'samples'})
        self.assertEqual(active_services(profile, ['step']), {'steps'})
        machines = [fixture(name) for name in NAMES]  # every kind, as a full collection holds
        self.assertTrue(any(m.kind == 'sample' for m in machines))
        steps = lambda selection: [step for _name, m, step in native.plan(selection) if m is None]
        self.assertEqual(steps(machines), list(native.SDK_UNITS))  # full collections keep the link order
        without = [m for m in machines if m.kind not in ('sample', 'step')]
        self.assertEqual(steps(without), [u for u in native.SDK_UNITS if u not in native.UNIT_SERVICES])
        self.assertEqual(set(native.object_services(machines).values()), {'samples', 'steps'})
        self.assertEqual(native.object_services(without), {})
        only_steps = [m for m in machines if m.kind != 'sample']
        self.assertEqual(set(native.object_services(only_steps).values()), {'steps'})

    def test_only_a_service_edit_may_stay_stock(self):
        profile = copy.deepcopy(config())
        tagged = next(p for p in profile['patches'] if p.get('service'))
        plain = next(p for p in profile['patches'] if p['kind'] == 'hook' and not p.get('service'))
        tagged['after'] = tagged['before']
        check_patch_boundaries(profile, len(profile['patches']))
        plain['after'] = plain['before']
        with self.assertRaisesRegex(ValueError, 'Invalid instruction edit'):
            check_patch_boundaries(profile, len(profile['patches']))
        plain.pop('after'); tagged['service'] = 'missing'
        with self.assertRaisesRegex(ValueError, 'Undeclared service'):
            check_patch_boundaries(profile, len(profile['patches']))


class SourceContracts(unittest.TestCase):
    def test_linker_layout_is_the_reviewed_one(self):
        profile = config()
        self.assertEqual(sha((ROOT / 'linker/opxy-1.2.1.ld').read_bytes()), profile['linker_sha256'])

    def test_profile_is_authenticated(self):
        original = pipeline.read
        changed = original(ROOT / pipeline.PROFILE).replace(b'"table_bytes": 8192', b'"table_bytes": 4096')
        with mock.patch.object(pipeline, 'read', side_effect=lambda path: changed if Path(path).name.endswith(
                '.json') else original(path)), self.assertRaisesRegex(ValueError, 'profile changed'):
            config()

    def test_edits_need_a_kind_and_reviewed_bytes(self):
        profile = config()
        count = len(profile['patches'])
        fixed = next(i for i, p in enumerate(profile['patches']) if p['kind'] in ('heap', 'table'))
        hook = next(i for i, p in enumerate(profile['patches']) if p['kind'] == 'hook')
        for index, change in ((fixed, dict(kind='other')), (fixed, dict(after=None)), (hook, dict(symbol='')),
                              (hook, dict(call=1)), (fixed, dict(after=profile['patches'][fixed]['before']))):
            altered = copy.deepcopy(profile)
            altered['patches'][index].update(change)
            altered['patches'][index] = {k: v for k, v in altered['patches'][index].items() if v is not None}
            with self.subTest(index=index, change=change), self.assertRaises(ValueError):
                check_patch_boundaries(altered, count)

    def test_protected_ranges_reject_profile_edits(self):
        profile = config()
        for start, end in PROTECTED:
            with self.subTest(address=hex(start)):
                altered = copy.deepcopy(profile)
                altered['patches'][0]['address'] = hex(start)
                with self.assertRaises(ValueError):
                    check_patch_boundaries(altered, len(profile['patches']))
        altered = copy.deepcopy(profile)
        altered['patches'][1] = copy.deepcopy(altered['patches'][0])
        with self.assertRaises(ValueError):
            check_patch_boundaries(altered, len(profile['patches']))

    def test_path_escape_and_absolute_paths_rejected(self):
        for name in ('../os.ldr', '/os.ldr', 'C:/os.ldr', 'a/../../x', 'a\\x', 'a//x', './x', ''):
            with self.subTest(name=name), self.assertRaises(ValueError):
                child(ROOT, name)

    def test_output_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'existing'
            path.mkdir()
            (path / 'keep.txt').write_text('keep')
            with self.assertRaises(ValueError):
                fresh_output(path)
            self.assertEqual((path / 'keep.txt').read_text(), 'keep')

    def test_release_host_has_tools_and_compiler(self):
        pins = pipeline.toolchains()
        release = pins['hosts'][pins['release']]
        self.assertTrue(release['tools'] and release['compiler'])
        for drop in ('compiler', 'executable_suffix'):
            broken = copy.deepcopy(pins)
            del broken['hosts'][pins['release']][drop]
            with self.subTest(drop=drop), self.assertRaises(ValueError):
                pipeline.check_toolchains(broken)
        with mock.patch.object(pipeline, 'read', return_value=b'{}'), self.assertRaises(ValueError):
            pipeline.toolchains()

    def test_msys_python_is_a_windows_host(self):
        for system in ('MSYS_NT-10.0-26200', 'CYGWIN_NT-10.0', 'MINGW64_NT-10.0'):
            with self.subTest(system=system), mock.patch('platform.system', return_value=system),                     mock.patch('platform.machine', return_value='x86_64'):
                self.assertEqual(pipeline.host_platform(), 'windows-x86_64')

    def test_release_kits_only_on_release_host(self):
        release = pipeline.toolchains()['release']
        with mock.patch.object(pipeline, 'host_platform', return_value=release):
            pipeline.require_release_host()
        with mock.patch.object(pipeline, 'host_platform', return_value='other-host'),                 self.assertRaisesRegex(ValueError, 'on ' + release + ', not other-host'):
            pipeline.require_release_host()


if __name__ == '__main__':
    unittest.main()
