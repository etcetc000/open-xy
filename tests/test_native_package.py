"""Private-fixture package verifier attack tests; never write a device or original package.

The CLI's real independent compile is exercised by build/verify. These tests
cache that known-good compiler output and isolate the package comparison logic,
so each corruption need not launch the compiler again.
"""
import copy
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

from tooling import native
from tooling.formats import elf32, elf_sections, sha
from tooling.pipeline import document, read, save_json


def object_symbols(data):
    """(defined global names, undefined names) of a relocatable ELF32 object."""
    headers = [h for _at, h in elf_sections(data)]
    defined, undefined = set(), set()
    for h in headers:
        if h[1] != 2:  # SHT_SYMTAB
            continue
        strings = headers[h[6]]
        for at in range(h[4], h[4] + h[5], 16):
            index, _value, _size, info, _other, section = struct.unpack_from('<IIIBBH', data, at)
            end = data.index(b'\0', strings[4] + index)
            name = data[strings[4] + index:end].decode('ascii')
            if name and section == 0:
                undefined.add(name)
            elif name and info >> 4 in (1, 2):  # global or weak
                defined.add(name)
    return defined, undefined


@unittest.skipUnless(os.environ.get('OPXY_TEST_NATIVE_PACKAGE') and os.environ.get('OPXY_TEST_STOCK_OS') and
                     os.environ.get('OPXY_TEST_STOCK_CONTENT'), 'Private package and stock inputs not supplied')
class PackageVerification(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = Path(os.environ['OPXY_TEST_NATIVE_PACKAGE']).resolve()
        cls.stock = read(Path(os.environ['OPXY_TEST_STOCK_OS']))
        cls.catalog = read(Path(os.environ['OPXY_TEST_STOCK_CONTENT']) / 'op1_factory.json')
        cls.module = read(cls.package / 'analysis/module.bin')
        cls.report = document(cls.package / 'analysis/compile-report.json')
        cls.elf = read(cls.package / 'analysis/module.elf')
        cls.patch_profile, cls.machines, _definitions = native.configuration(native.package_collection(cls.package))
        cls.patch_profile['patches'] = cls.report['patches']

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='opxy-tamper-')
        self.addCleanup(self.temporary.cleanup)
        self.copy = Path(self.temporary.name) / 'package'
        shutil.copytree(self.package, self.copy)

    def fresh_compile(self, directory, *_args):
        (directory / 'module.elf').write_bytes(self.elf)
        return self.module, copy.deepcopy(self.report), copy.deepcopy(self.patch_profile)

    def verify(self):
        with patch.object(native, 'compile_collection', self.fresh_compile):
            options = [Path('unused-mocked-toolchain')] * 2
            return native.verification(self.stock, self.catalog, self.copy, *options)

    def change_hash(self, name):
        manifest = document(self.copy / 'manifest.json')
        manifest['files'][name] = sha(read(self.copy / name))
        save_json(self.copy / 'manifest.json', manifest)

    def test_original_fixture_verifies(self):
        result = self.verify()
        self.assertTrue(result['fresh_source_rebuild_matches'])
        self.assertFalse(result['hardware_tested'])

    def test_profiling_mode_is_part_of_verified_provenance(self):
        path = self.copy / 'manifest.json'
        manifest = document(path)
        original = manifest['profiling']
        for value in (not original, None, 0, 1, 'off'):
            with self.subTest(profiling=value):
                manifest['profiling'] = value
                save_json(path, manifest)
                with self.assertRaises(ValueError):
                    self.verify()

    def test_profiling_hooks_match_mode_and_stay_in_platform_unit(self):
        symbols, _undefined = object_symbols(read(self.package / 'analysis/sdk/profiling.o'))
        for edit in self.report['patches']:
            if edit.get('profiling') is False:
                self.assertIn(edit['symbol'], symbols)
                self.assertEqual(edit['after'] == edit['before'], self.report['profiling'])

    def test_service_hooks_live_in_their_units_and_only_their_users_reach_them(self):
        services = native.object_services(self.machines)
        kinds = {m.id: m.kind for m in self.machines}
        owner_of, needs = {}, {}
        for name, owner in self.report['objects'].items():
            defined, undefined = object_symbols(read(self.package / 'analysis' / name))
            owner_of.update(dict.fromkeys(defined, name))
            needs[name] = (owner, undefined)
        for patch in self.report['patches']:
            if patch.get('service') and patch['after'] != patch['before']:
                self.assertEqual(services.get(owner_of[patch['symbol']]), patch['service'], patch['symbol'])
        # Only a service's own units and machines of the kinds it serves may use its symbols.
        users = self.patch_profile['services']
        for name, (owner, undefined) in needs.items():
            for symbol in undefined:
                service = services.get(owner_of.get(symbol))
                if service and name not in services:
                    self.assertIn(kinds.get(owner), users[service], (name, symbol))

    def test_only_curated_engines_are_linked_and_added_to_stock_content(self):
        _, symbols = elf32(self.elf)
        engines = [m for m in self.machines if m.kind != 'step']
        for m in engines:
            self.assertIn(m.prefix + ('_fx_factory' if m.kind == 'effect' else '_factory'), symbols)
        factories = {name for name in symbols if name.endswith('_factory')}
        self.assertEqual(factories, {m.prefix + ('_fx_factory' if m.kind == 'effect' else '_factory')
                                     for m in engines})
        # A step machine has no factory or menu entry: its replaced component's method points at its adapter.
        replaced = {m.identity['step']: m for m in self.machines if m.kind == 'step'}
        for patch in self.report['patches']:
            if patch['kind'] == 'step':
                word = int.from_bytes(bytes.fromhex(patch['after'])[:4], 'little')
                if patch['slot'] not in replaced:
                    self.assertEqual(patch['after'], patch['before'])
                elif 'apply' in patch['words']:
                    at = 4 * patch['words'].index('apply')
                    word = int.from_bytes(bytes.fromhex(patch['after'])[at:at + 4], 'little')
                    self.assertEqual(word, symbols[replaced[patch['slot']].prefix + '_step_apply']['value'])
        original = json.loads(self.catalog)
        current = json.loads((self.package / 'candidate/content/op1_factory.json').read_bytes())
        names = [m.native_name for m in self.machines if m.kind in ('synth', 'sample')]
        self.assertEqual(current['engine_types'][:-len(names)], original['engine_types'])
        self.assertEqual([e['type'] for e in current['engine_types'][-len(names):]], names)
        displays = [m.native_name for m in self.machines if m.display != 'stock']
        files = {p.relative_to(self.package / 'candidate').as_posix()
                 for p in (self.package / 'candidate').rglob('*') if p.is_file()}
        self.assertEqual(files, {'os.ldr', 'content/op1_factory.json'} | {f'content/display/{n}.svg' for n in displays})

    def test_changed_os_elf_module_and_asset_fail_even_with_new_hash(self):
        names = ['candidate/os.ldr', 'analysis/module.elf', 'analysis/module.bin', 'candidate/content/op1_factory.json',
                 *(f'candidate/content/display/{m.native_name}.svg' for m in self.machines if m.display != 'stock')]
        for name in names:
            with self.subTest(name=name):
                path = self.copy / name
                original = path.read_bytes()
                changed = bytearray(original); changed[len(changed) // 2] ^= 1
                path.write_bytes(changed); self.change_hash(name)
                with self.assertRaises(ValueError): self.verify()
                path.write_bytes(original); self.change_hash(name)

    def test_false_hardware_and_native_provenance_fail(self):
        cases = (('verification.json', 'hardware_tested'),
                 ('analysis/compile-report.json', 'hardware_tested'))
        for name, key in cases:
            with self.subTest(name=name):
                path = self.copy / name; original = path.read_bytes()
                value = json.loads(original); value[key] = True; save_json(path, value)
                if name.startswith('analysis/'):
                    self.change_hash(name)
                with self.assertRaises(ValueError): self.verify()
                path.write_bytes(original)
                if name.startswith('analysis/'):
                    self.change_hash(name)

    def test_additional_deployment_file_and_manifest_escape_fail(self):
        (self.copy / 'candidate/bootloader.ldr').write_bytes(b'not deployment evidence')
        with self.assertRaises(ValueError): self.verify()
        (self.copy / 'candidate/bootloader.ldr').unlink()
        manifest = document(self.copy / 'manifest.json')
        manifest['files']['../outside'] = '0' * 64
        save_json(self.copy / 'manifest.json', manifest)
        with self.assertRaises(ValueError): self.verify()


if __name__ == '__main__':
    unittest.main()
