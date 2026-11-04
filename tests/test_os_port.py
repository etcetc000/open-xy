"""The OS port report and the CPU run comparison, on authored loader images and captures:
no stock firmware or device."""
import json
from pathlib import Path
import random
import struct
import sys
import tempfile
import unittest
from unittest import mock

from tooling.formats import header
from tooling.pipeline import ROOT

sys.path.insert(0, str(ROOT / 'tools'))
import cpu_compare  # noqa: E402
import os_port  # noqa: E402

HEAP, HEAP_BYTES, RESERVED = 0x8c0d0000, 0x10000, 0x4000
SITE, FUNCTION, LOAD, RAM = 1000, 3000, 2000, 0x08031234
RELOCATED = HEAP + HEAP_BYTES - 0x100  # a table in the reservation, just below code


def call(site, target):
    delta = (target - site) // 2 & 0xffffff
    return struct.pack('<HH', 0xe300 | delta >> 16, delta & 0xffff)


def load(value, register=2):
    return struct.pack('<4H', 0xda00 | register, value >> 16, value & 0xffff, 0)


def image(code, heap_base):
    """An authored loader: a heap fill ending where code starts, the code, then FINAL."""
    start = heap_base + HEAP_BYTES
    return (header(0x104, heap_base, HEAP_BYTES, 0) + header(4, start, len(code), 0) + bytes(code) +
            header(0x8004, 0, 0, 0)), start


def program(inserted, constant):
    """Code with a call at SITE to FUNCTION and a table base load at LOAD; `inserted` bytes
    of new code before them move both."""
    rng = random.Random(7)
    body = bytearray(rng.getrandbits(8) for _ in range(4096))
    body[FUNCTION: FUNCTION + 64] = bytes(range(64))
    code = bytearray(rng.getrandbits(8) for _ in range(inserted)) + body
    return code, inserted


class OsPort(unittest.TestCase):
    def setUp(self):
        old, _ = program(0, RAM)
        new, shift = program(100, RAM + 0x4444)
        self.shift = shift
        # The heap shrinks by 0x40 in the target, so code and the reservation below it move.
        code, new_code = HEAP + HEAP_BYTES, HEAP - 0x40 + HEAP_BYTES
        for data, base, moved in ((old, code, 0), (new, new_code, shift)):
            data[SITE + moved: SITE + moved + 4] = call(base + SITE + moved, base + FUNCTION + moved)
            data[LOAD + moved: LOAD + moved + 8] = load(RAM if not moved else RAM + 0x4444)
        source, code = image(old, HEAP)
        target, new_code = image(new, HEAP - 0x40)
        self.source, self.target = os_port.Image(data=source), os_port.Image(data=target)
        self.code, self.new_code = code, new_code
        offset = lambda address: self.source.offset(address)
        self.profile = dict(heap_fill_base=HEAP, reserved_bytes=RESERVED, patches=[
            dict(kind='hook', symbol='sdk_probe', call=True, address=hex(code + SITE), length=4,
                 file_offset=hex(offset(code + SITE)), before=call(code + SITE, code + FUNCTION).hex()),
            dict(kind='table', name='probe table base', address=hex(code + LOAD), length=8,
                 file_offset=hex(offset(code + LOAD)), before=load(RAM).hex(), after=load(RELOCATED).hex())])

    def test_edits_follow_moved_code_and_the_reservation(self):
        loads = os_port.constant_loads(self.source), os_port.constant_loads(self.target)
        hook, table = os_port.port_edits(self.source, self.target, self.profile, loads)
        self.assertEqual(hook['status'], 'verified')
        self.assertEqual(int(hook['target_address'], 16), self.new_code + SITE + self.shift)
        self.assertEqual(table['status'], 'located')  # the stock table it loads moved
        old_fill = os_port.heap_fill(self.source, base=HEAP)
        new_fill = os_port.heap_fill(self.target, near=old_fill['count'])
        old_end, new_end = HEAP + HEAP_BYTES, new_fill['target'] + new_fill['count']
        after = os_port.candidate_table_after(self.profile['patches'][1], bytes.fromhex(table['target_before']),
                                              (old_end - RESERVED, old_end), new_end - old_end)
        self.assertEqual(after, load(RELOCATED + new_end - old_end).hex())
        with self.assertRaisesRegex(ValueError, 'heap_fill_base'):
            os_port.heap_fill(self.target, base=HEAP)

    def test_the_report_stays_out_of_checkouts_and_carries_the_target_layout(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'source.ldr').write_bytes(self.source.data)
            (root / 'target.ldr').write_bytes(self.target.data)
            (root / 'profile.json').write_text(json.dumps(self.profile))
            args = ['--source', str(root / 'source.ldr'), '--target', str(root / 'target.ldr'),
                    '--profile', str(root / 'profile.json')]
            with self.assertRaisesRegex(ValueError, 'outside every checkout'):
                os_port.main(args + ['--out', str(ROOT / 'local' / 'port-test')])
            self.assertFalse((ROOT / 'local' / 'port-test').exists())
            with mock.patch('builtins.print'):
                os_port.main(args + ['--out', str(root / 'out')])
            candidate = json.loads((root / 'out' / 'profile-candidate.json').read_text())
            self.assertEqual(candidate['heap_fill_base'], HEAP - 0x40)
            self.assertEqual(candidate['module_base'], self.new_code - RESERVED)
            self.assertIsNone(candidate['id'])
            self.assertEqual([p['port_status'] for p in candidate['patches']], ['verified', 'located'])


def capture(identity, cases, value, voice=True):
    return dict(mode='benchmark', package=identity, expected_cases=cases, value=value, voice=voice)


class CpuCompare(unittest.TestCase):
    cases = [dict(name='prism')]

    def run_medians(self, captures, identity='build'):
        def decode(c, expected):
            return [dict(machine=dict(name='prism'), insufficient_voice_activity=not c['voice'],
                         metrics=[dict(index=15, encoded_average=1, average_budget_percent=c['value'])])]
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.object(cpu_compare, 'package_data', return_value=({}, dict(benchmark_cases=self.cases), identity)), \
                mock.patch.object(cpu_compare, 'decode', decode):
            runs = []
            for i, c in enumerate(captures):
                run = Path(folder) / str(i)
                run.mkdir()
                (run / 'capture.json').write_text(json.dumps(c))
                runs.append(run)
            return cpu_compare.medians('package', runs)[0]

    def test_runs_are_this_package_used_once_and_all_sounded(self):
        self.assertEqual(self.run_medians([capture('build', self.cases, v) for v in (1.0, 3.0, 2.0)]),
                         {('prism', 15): 2.0})
        with self.assertRaisesRegex(ValueError, 'given twice'):
            self.run_medians([capture('build', self.cases, 1.0)] * 2)
        with self.assertRaisesRegex(ValueError, 'another build'):
            self.run_medians([capture('other', self.cases, 1.0)])
        # A run whose voice never sounded leaves the case out rather than skewing it.
        with self.assertRaisesRegex(ValueError, 'every run'):
            self.run_medians([capture('build', self.cases, 1.0), capture('build', self.cases, 2.0, voice=False)])

    def test_a_sweep_build_keeps_each_synth_run_apart(self):
        setting = dict(control=0, label='Tone', value=0x7fffffff)
        cases = [dict(name='pluck', source='sdk', kind='synth'),
                 dict(name='pluck', source='sdk', kind='synth', setting=setting),
                 dict(name='pluck', source='sdk', kind='synth', combined=True)]

        def decode(c, expected):
            return [dict(machine=case, insufficient_voice_activity=False,
                         metrics=[dict(index=5, encoded_average=1, average_budget_percent=c['value'] + i)])
                    for i, case in enumerate(expected)]
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.object(cpu_compare, 'package_data', return_value=({}, dict(benchmark_cases=cases), 'build')), \
                mock.patch.object(cpu_compare, 'decode', decode):
            runs = []
            for i, value in enumerate((1.0, 2.0, 3.0)):
                run = Path(folder) / str(i)
                run.mkdir()
                (run / 'capture.json').write_text(json.dumps(capture('build', cases, value)))
                runs.append(run)
            self.assertEqual(cpu_compare.medians('package', runs)[0],
                             {('pluck midpoint (stock)', 5): 2.0, ('pluck #0 Tone 100.0%', 5): 3.0,
                              ('pluck combined', 5): 4.0})


if __name__ == '__main__':
    unittest.main()
