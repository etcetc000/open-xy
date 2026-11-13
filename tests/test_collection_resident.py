"""Resident hook admission and linked Blackfin instruction contracts."""
import copy
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest

from tooling import collection_activation as activation, collection_resident as resident
from tooling.formats import elf32, elf_sections
from tooling.pipeline import config


class Resident(unittest.TestCase):
    def interface(self, profiling=True):
        profile=config()
        return activation.interface(profile, set(profile['services']), profiling, 32768)

    def test_each_supported_hook_has_an_exact_stock_path(self):
        for profiling in (True, False):
            with self.subTest(profiling=profiling):
                abi = self.interface(profiling)
                kinds = {resident.stock_path(e)['kind'] for e in abi['entries']}
                self.assertEqual(kinds, {'branch', 'replay', 'return', 'conditional', 'call_suffix', 'call_prefix'})
                source = resident.guarded_dispatch(abi)
                self.assertEqual(source.count('    [--SP] = ASTAT;'), len(abi['entries']))
                self.assertEqual(source.count('    ASTAT = [SP++];'), len(abi['entries'])*2)
                self.assertNotIn('    CALL ', source)
                self.assertIn('.bss.collection_resident', source)

    def test_unknown_displaced_code_and_changed_return_modes_are_refused(self):
        abi = self.interface()
        for kind in ('branch', 'replay', 'return', 'conditional', 'call_suffix', 'call_prefix'):
            entry = copy.deepcopy(next(e for e in abi['entries'] if resident.stock_path(e)['kind'] == kind))
            entry['call'] = not entry['call']
            with self.assertRaises(ValueError): resident.stock_path(entry)
        entry = dict(site='0x8d506a02', before='00000000', call=False, length=4)
        with self.assertRaises(ValueError): resident.stock_path(entry)
        entry['before'] = resident.REPLAY[0x8d506a02][0]
        entry['site'] = '0x8d506a04'
        with self.assertRaises(ValueError): resident.stock_path(entry)

    def test_displaced_conditional_targets_original_stock_address(self):
        site, before, call = resident.CONDITIONAL
        path = resident.stock_path(dict(site=hex(site), before=before, call=call, length=8))
        self.assertEqual(path['target'], 0x8c0b148c)
        self.assertEqual(path['opcode'], 0x1000)
        site, before, call = resident.CONDITIONAL_JUMP
        path = resident.stock_path(dict(site=hex(site), before=before, call=call, length=4))
        self.assertEqual(path['raw'], bytes.fromhex('000c'))
        self.assertEqual(path['target'], 0x8d5aca80)
        self.assertEqual(path['opcode'], 0x1800)

    def test_shift_press_relocates_its_trailing_jump(self):
        site, before, call = resident.SHIFT_PRESS_CALL_SUFFIX
        entry = dict(site=hex(site), before=before, call=call, length=8)
        path = resident.stock_path(entry)
        self.assertEqual(path, dict(kind='call_suffix', target=0x8c0b270e,
                                    raw=bytes.fromhex('0160'), continuation=0x8c0b28f6))
        for offset in range(8):
            changed = bytearray.fromhex(before)
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                resident.stock_path(dict(entry, before=changed.hex()))

    def test_resident_layout_keeps_the_original_pool_tables_and_payload_boundary(self):
        from tooling.pipeline import ROOT
        source=(ROOT/'linker/opxy-1.2.1.ld').read_text()
        changed=resident.linker(source,config())
        self.assertIn('.text 0x8d2d8380 : { KEEP(sdk/collection_entries.o(',changed)
        for line in source.splitlines():
            if line.strip().startswith(('MEMORY','__stock_','ASSERT(')):
                self.assertIn(line,changed)
        self.assertIn('.resident_bss (NOLOAD)',changed)
        with self.assertRaises(ValueError): resident.linker(source+'\n',config())

    @unittest.skipUnless(os.environ.get('OPXY_TEST_TOOLCHAIN'), 'No pinned Blackfin tools fixture')
    def test_native_cache_operations_and_isolated_scratch_are_exact(self):
        from tooling.pipeline import ROOT
        tools=Path(os.environ['OPXY_TEST_TOOLCHAIN'])
        suffix='.exe' if os.name=='nt' else ''
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);script=folder/'cache.ld';obj=folder/'cache.o';elf=folder/'cache.elf'
            script.write_text('OUTPUT_ARCH(bfin)\nSECTIONS { . = 0x8d2d0380; .text : { *(.text .text.*) } }\n')
            subprocess.run([str(tools/('bfin-elf-as'+suffix)),'-mcpu=bf707','-o',str(obj),
                            str(ROOT/'sdk/platform/opxy-1.2.1/collection_cache.s')],check=True,capture_output=True)
            subprocess.run([str(tools/('bfin-elf-ld'+suffix)),'-T',str(script),'-o',str(elf),str(obj)],
                           check=True,capture_output=True)
            data=elf.read_bytes();abi=self.interface();checked=resident.check_cache_operations(data,abi)
            self.assertEqual(checked['sync_bytes'],62);self.assertEqual(checked['scratch']%32,0)
            sections,symbols=elf32(data);text=sections['.text']
            header=next(h for _,h in elf_sections(data) if h[1]==1 and h[3]==text['address'])
            for name in ('sdk_collection_sync_code','sdk_collection_cache_scratch',
                         'sdk_collection_try_lock','sdk_collection_unlock'):
                symbol=symbols[name]
                for offset in range(symbol['size']):
                    changed=bytearray(data)
                    changed[header[4]+symbol['value']-text['address']+offset]^=1
                    with self.assertRaises(ValueError): resident.check_cache_operations(changed,abi)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_TOOLCHAIN'), 'No pinned Blackfin tools fixture')
    def test_actual_linked_guards_and_stock_paths_reject_instruction_corruption(self):
        tools = Path(os.environ['OPXY_TEST_TOOLCHAIN'])
        suffix = '.exe' if os.name == 'nt' else ''
        abi = self.interface()
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            source, obj, elf = (folder/name for name in ('guards.s','guards.o','guards.elf'))
            source.write_text(resident.guarded_dispatch(abi))
            script = folder/'resident.ld'
            script.write_text('OUTPUT_ARCH(bfin)\nSECTIONS { . = 0x8d2d0380; '
                              '.text : { *(.text .text.*) } .bss : { *(.bss .bss.*) } }\n')
            subprocess.run([str(tools/('bfin-elf-as'+suffix)), '-mcpu=bf707','-o',str(obj),str(source)],
                           check=True, capture_output=True)
            subprocess.run([str(tools/('bfin-elf-ld'+suffix)), '-T',str(script),'-o',str(elf),str(obj)],
                           check=True, capture_output=True)
            data = elf.read_bytes()
            checked = resident.check_guarded_dispatch(data, abi)
            self.assertEqual(checked['entries'], len(abi['entries']))
            sections, symbols = elf32(data)
            headers = [h for _,h in elf_sections(data)]
            # Corrupt every guard's save, flag address, comparison, both restore
            # paths and both branch targets, plus each displaced stock path.
            text = sections['.text']
            header = next(h for h in headers if h[3] == text['address'] and h[1] == 1)
            for i in range(len(abi['entries'])):
                for name, offsets in ((f'sdk_collection_resident_{i}', (0,6,14,18,20,26,34,38)),
                                      (f'sdk_collection_stock_{i}', (0,))):
                    symbol = symbols[name]
                    for offset in offsets:
                        changed = bytearray(data)
                        changed[header[4]+symbol['value']-text['address']+offset] ^= 1
                        with self.assertRaises(ValueError): resident.check_guarded_dispatch(changed, abi)
            shifted = copy.deepcopy(abi)
            shifted['geometry']['payload_base'] += 32
            with self.assertRaises(ValueError): resident.check_guarded_dispatch(data, shifted)
            for offset,h in elf_sections(data):
                if h[1] not in (1,8) or not h[2] & 2:
                    continue
                # Unallocated code/BSS is absent from the loaded module; a
                # verifier must not accept bytes merely present in an ELF.
                changed = bytearray(data)
                struct.pack_into('<I', changed, offset+8, h[2] & ~2)
                with self.assertRaises(ValueError): resident.check_guarded_dispatch(changed, abi)


if __name__ == '__main__': unittest.main()
