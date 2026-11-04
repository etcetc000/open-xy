"""File banks (opxy/file_bank.hpp, tooling/file_banks.py): containers, device
paths, compiled data, shared files, the native table and the loader."""
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from machine_fixtures import FOLDER, fixture
from test_sample_services import HOST_CXX
from tooling import adapters, file_banks, machines, native
from tooling.pipeline import ROOT


def object_data(name='words', payload=b'\1\0\0\0', flags=2, alignment=32, relocated=False, array_bytes=None):
    """A minimal relocatable ELF32 with one file bank section and its array symbol."""
    labels = b'\0.shstrtab\0' + (file_banks.PREFIX + name).encode() + b'\0.rel.bank\0'
    count = 5 if relocated else 4
    data = bytearray(52 + count * 40)
    data[:6] = b'\x7fELF\1\1'
    struct.pack_into('<H', data, 18, 106)
    struct.pack_into('<I', data, 32, 52)
    struct.pack_into('<HHH', data, 46, 40, count, 1)
    offset = len(data)
    data += labels
    struct.pack_into('<10I', data, 92, 1, 3, 0, 0, offset, len(labels), 0, 0, 1, 0)
    offset = len(data)
    data += payload
    struct.pack_into('<10I', data, 132, 11, 1, flags, 0, offset, len(payload), 0, 0, alignment, 0)
    offset = len(data)
    data += struct.pack('<IIIBBH', 0, 0, len(payload) if array_bytes is None else array_bytes, 0x11, 0, 2)
    struct.pack_into('<10I', data, 172, 0, 2, 0, 0, offset, 16, 1, 0, 4, 16)
    if relocated:
        offset = len(data)
        data += bytes(8)
        struct.pack_into('<10I', data, 212, 11 + len(file_banks.PREFIX + name) + 1, 9, 0, 0, offset, 8, 0, 2, 4, 8)
    return bytes(data)


def banked(machine_id='probesynth', banks=(('words_view', 'words'),)):
    return replace(fixture('probesynth'), id=machine_id, file_banks=banks)


class Containers(unittest.TestCase):
    def test_container_is_content_addressed_and_validated(self):
        payload = bytes(range(256)) * 4
        data = file_banks.container(payload)
        digest = hashlib.sha256(payload).hexdigest()
        self.assertEqual(len(data), 128 + len(payload))
        self.assertEqual(file_banks.validate(data, digest), payload)
        for offset in (0, 8, 16, 48, 80, 84, 88, 100, 130, len(data) - 1):
            changed = bytearray(data)
            changed[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                file_banks.validate(bytes(changed), digest)
        for bad in (b'', b'abc', bytes(file_banks.MAX_PAYLOAD + 4)):
            with self.assertRaises(ValueError):
                file_banks.container(bad)

    def test_the_device_opens_banks_through_the_yaffs2_mount(self):
        # A bare /content/... path never opens on the device (troupe-bounded-bank-file-005).
        digest = 'ab' * 32
        path = file_banks.package_path(digest)
        self.assertEqual(path, f'content/banks/shared/{digest}-bank.hb')
        self.assertEqual(file_banks.device_path(path), '/yaffs2/' + path)
        for bad in ('/content/banks/shared/x.hb', 'content/banks/troupe/' + digest + '-bank.hb',
                    'content/banks/shared/../' + digest + '-bank.hb'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                file_banks.device_path(bad)

    def test_container_matches_the_transfer_tools_bank_identity(self):
        # The verified device-transfer tool binds owner = folder and name = file suffix.
        data = file_banks.container(b'\1\2\3\4')
        self.assertEqual(data[16:48].rstrip(b'\0'), b'shared')
        self.assertEqual(data[48:80].rstrip(b'\0'), b'bank')


class CompiledBanks(unittest.TestCase):
    def test_only_exact_unrelocated_word_arrays_are_exported(self):
        self.assertEqual(file_banks.inspect(object_data(), allocated=True)['words']['payload'], b'\1\0\0\0')
        for changes in (dict(flags=3), dict(alignment=4), dict(payload=b'abc'), dict(relocated=True),
                        dict(name='Words')):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                file_banks.inspect(object_data(**changes), allocated=True)

    def test_detached_banks_take_no_module_memory(self):
        data = object_data(payload=bytes(64))
        self.assertEqual(native.object_memory(data), (64, 0))
        detached = file_banks.detach(data)
        self.assertEqual(native.object_memory(detached), (0, 0))
        self.assertEqual(file_banks.inspect(detached, allocated=False)['words']['payload'], bytes(64))

    def test_declared_and_compiled_banks_agree_and_identical_tables_share_a_file(self):
        shared, own = b'\1\0\0\0' * 8, b'\2\0\0\0' * 8
        first = banked('first', (('words_view', 'words'), ('cast_view', 'cast')))
        second = banked('second', (('table_view', 'table'),))
        with tempfile.TemporaryDirectory() as directory:
            for m, objects in ((first, [object_data('words', shared), object_data('cast', own)]),
                               (second, [object_data('table', shared)])):
                folder = Path(directory) / m.id
                folder.mkdir()
                for index, data in enumerate(objects):
                    (folder / f'unit{index}.o').write_bytes(file_banks.detach(data))  # as the build leaves them
            files = file_banks.compiled(directory, [first, second])
            self.assertEqual(len(files), 2)
            by_digest = {f['digest']: f for f in files}
            common = by_digest[hashlib.sha256(shared).hexdigest()]
            self.assertEqual(sorted(u[3] for u in common['uses']), ['first', 'second'])
            self.assertEqual(file_banks.validate(common['data'], common['digest']), shared)
            table = file_banks.generate(directory, files, gui=False).read_text()
            self.assertIn('"/yaffs2/content/banks/shared/' + common['digest'] + '-bank.hb"', table)
            self.assertNotIn('"/content/', table)
            self.assertIn('native_file_bank_count = 2;', table)
            self.assertIn('native_file_bank_use_count = 3;', table)
            self.assertIn(f'native_file_bank_budget = {2 * (32 + file_banks.ALLOWANCE)};', table)
            self.assertIn('native_file_banks_gui = false;', table)
            self.assertIn('native_file_banks_gui = true;', file_banks.generate(directory, files, gui=True).read_text())
            for wrong in (replace(first, file_banks=(('words_view', 'words'),)),
                          replace(first, file_banks=(('words_view', 'words'), ('cast_view', 'other')))):
                with self.subTest(wrong=wrong.file_banks), self.assertRaises(ValueError):
                    file_banks.compiled(directory, [wrong])

    def test_banks_beyond_the_session_cap_are_refused(self):
        m = banked(banks=tuple((f'view{i}', f'bank{i}') for i in range(5)))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / m.id
            folder.mkdir()
            for i in range(5):
                payload = struct.pack('<I', i) * (file_banks.MAX_PAYLOAD // 4)
                (folder / f'unit{i}.o').write_bytes(file_banks.detach(object_data(f'bank{i}', payload)))
            with self.assertRaises(ValueError):
                file_banks.compiled(directory, [m])


class MachineContract(unittest.TestCase):
    def machine_with(self, **changes):
        temporary = Path(tempfile.mkdtemp()).resolve()
        self.addCleanup(shutil.rmtree, temporary)
        root = temporary / 'probesynth'
        shutil.copytree(FOLDER / 'probesynth', root)
        path = root / 'machine.json'
        value = json.loads(path.read_text())
        value.pop('identity', None)  # from SDK 1.5 a build gives the numbers
        value.update(changes)
        path.write_text(json.dumps(value))
        return machines.machine_at(root)

    def test_file_banks_need_sdk_1_10_and_name_distinct_views_and_arrays(self):
        m = self.machine_with(sdk='1.10', fileBanks={'words_view': 'words', 'cast_view': 'cast'})
        self.assertEqual(m.file_banks, (('words_view', 'words'), ('cast_view', 'cast')))
        for changes in (dict(sdk='1.9', fileBanks={'words_view': 'words'}),
                        dict(sdk='1.10', fileBanks={}),
                        dict(sdk='1.10', fileBanks=['words']),
                        dict(sdk='1.10', fileBanks={'words': 'words'}),
                        dict(sdk='1.10', fileBanks={'a': 'words', 'b': 'words'}),
                        dict(sdk='1.10', fileBanks={'View': 'words'})):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.machine_with(**changes)


class Adapters(unittest.TestCase):
    def test_a_banked_synth_registers_after_loading_and_stays_silent_without_its_banks(self):
        m = banked(banks=(('words_view', 'words'), ('cast_view', 'cast')))
        with tempfile.TemporaryDirectory() as directory:
            registration = adapters.generate_registration(directory, m).read_text()
            voice = adapters.generate_voice(directory, m).read_text()
            (Path(directory) / 'plain').mkdir()
            plain_text = adapters.generate_voice(Path(directory) / 'plain', fixture('probesynth')).read_text()
        self.assertIn('sdk_file_banks_prepare() ? sdk_register_engine(&record) : 0', registration)
        self.assertIn('sdk_file_banks_prepare() ? sdk_register_screen(&record, &screen) : 0', registration)
        self.assertIn('#include "file_banks.hpp"', registration)
        guard = 'if (!probesynth::words_view.active || !probesynth::cast_view.active)'
        self.assertEqual(voice.count(guard), 2)
        self.assertNotIn('active', plain_text.replace('// ', ''))  # other synths are unchanged


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class Loader(unittest.TestCase):
    def test_every_failure_frees_its_block_and_leaves_views_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'loader'
            subprocess.run([HOST_CXX, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform'),
                            str(ROOT / 'tests/fixtures/file_bank_loader.cpp'), '-o', str(out)], check=True)
            result = subprocess.run([str(out)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), 'ok')


if __name__ == '__main__':
    unittest.main()
