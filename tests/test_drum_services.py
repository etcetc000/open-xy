"""Execute drum GUI routing with real SDK controls and stock boundary doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from types import SimpleNamespace

from tooling.adapters import generate_drum_callbacks

from test_sample_services import HOST_CXX, ROOT


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class DrumServices(unittest.TestCase):
    def test_generated_classification_does_not_require_gui_screen(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        machine = SimpleNamespace(kind='drum', prefix='probe', definition={'resources': {'modelCount': 12}})
        with tempfile.TemporaryDirectory(prefix='drum-classification-') as folder:
            folder = Path(folder)
            generated = generate_drum_callbacks(folder, [machine])
            binary = folder / ('classification.exe' if os.name == 'nt' else 'classification')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                       '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                       str(generated), str(platform / 'drum_machines.cpp'),
                       str(ROOT / 'tests/fixtures/drum_classification.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_kit_of_more_than_16_models_has_at_most_15_with_eight_controls(self):
        from tooling.machine_test import drum_storage
        with tempfile.TemporaryDirectory(prefix='drum-storage-') as folder:
            folder = Path(folder)
            (folder / 'core.h').write_text('#include <opxy/types.hpp>\nnamespace probe { opxy::u32 parameter_count(opxy::u32); }\n')
            for fulls, ok in ((16, False), (15, True), (14, True)):
                (folder / 'engine.cpp').write_text(
                    'namespace probe { opxy::u32 parameter_count(opxy::u32 m) { return m < %d ? 8 : 5; } }\n' % fulls)
                machine = SimpleNamespace(id='probe', root=folder, sources=(), includes=(),
                                          definition={'id': 'probe', 'resources': {'modelCount': 33}})
                with self.subTest(fulls=fulls):
                    if ok:
                        self.assertEqual(drum_storage(machine, HOST_CXX, folder), fulls)
                    else:
                        with self.assertRaisesRegex(ValueError, '16 of its 33 models use all eight controls'):
                            drum_storage(machine, HOST_CXX, folder)
            machine.definition['resources']['modelCount'] = 16
            (folder / 'engine.cpp').write_text('namespace probe { opxy::u32 parameter_count(opxy::u32) { return 8; } }\n')
            self.assertEqual(drum_storage(machine, HOST_CXX, folder), 16)   # the 4-bit layout holds any

    def test_pad_edit_alternates_shift_and_model_changes(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        with tempfile.TemporaryDirectory(prefix='drum-services-') as folder:
            folder = Path(folder)
            stock = (platform / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            # Keep the shared inline decoder beside the mocked stock header.
            (folder / 'drum_values.hpp').write_text((platform / 'drum_values.hpp').read_text())
            sources = []
            for name in ('synth_controls.cpp', 'drum_synthesis_stock.cpp', 'drum_values_stock.cpp', 'drum_machines.cpp'):
                target = folder / name
                target.write_text((platform / name).read_text())
                sources.append(str(target))
            binary = folder / ('drum_gui.exe' if os.name == 'nt' else 'drum_gui')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                       '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                       str(ROOT / 'tests/fixtures/drum_gui.cpp'), *sources, '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class PadSetupStock(unittest.TestCase):
    """drum_storage.hpp's set-up value against each stock writer of a kit key's pad word."""

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_no_stock_writer_of_the_pad_word_gives_the_set_up_value(self):
        import re
        from tooling.formats import ldr, mapped_offset, sha
        from tooling.pipeline import config
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), config()['stock_sha256'])
        blocks = ldr(stock)

        def code(address, length):
            offset = mapped_offset(blocks, address, length)
            return stock[offset:offset + length].hex()

        def word(address):
            # Stock's data mixes stored and filled load blocks.
            block = next(b for b in blocks if b['target'] <= address < b['target'] + b['count'])
            if block['fill']:
                return block['argument']
            offset = mapped_offset(blocks, address, 4)
            return int.from_bytes(stock[offset:offset + 4], 'little')

        header = (ROOT / 'sdk/platform/opxy-1.2.1/drum_storage.hpp').read_text(encoding='utf-8')
        ready = int(re.search(r'kDrumPadReady = (0x[0-9a-f]+);', header).group(1), 16)

        def is_ready(value):
            return value & 0x8000ffff == ready

        # Projects keep the record byte for byte: the pattern writer (8d598ce8)
        # hands the 24 pad entries at +0x394c, 0xc00 bytes, to its writer 8d5914bc.
        self.assertEqual(code(0x8d598e68, 16), '20e14c394650073022e1000cffe324c3')
        # The payload builder's word 6 is max(word +0x14, flag): R0 = [P4 + 0x14];
        # R0 = MAX (R0, R6); [P3 + 0x18] = R0.
        self.assertEqual(code(0x8d5ad738, 8), '60a107c4060098b1')
        # Preset import: a region's random as max(random << 16, 0) (8d4e3558),
        # stored at +0x14 (8d4e3b9e, [P5 + 0x10] with P5 the entry + 4); so a
        # preset leaves a low half of 0.
        self.assertEqual(code(0x8d4e3588, 30), '073000de058c1043000082ce8080f7a0000001e80000016007c401001000')
        self.assertEqual(code(0x8d4e3b94, 12), 'b1af09e46000ffe3dffc28b1')
        # Or its default kit's value from the table at 8403a7b4 (8d4e3956..3958).
        self.assertEqual(code(0x8d4e394a, 16), '31e5170030e51500e9b0a865099129b1')
        defaults = [word(0x8403a7b4 + 4 * k) for k in range(24)]
        self.assertEqual(defaults, [0] * 23 + [0x26660000])
        # Stock's Drumline editor (8d4d6098): 8d4d6cc8 with 100, which it doubles
        # to 200 steps, then b * q + q / 2 for q = 0x7fffffff / 200.
        self.assertEqual(code(0x8d4d6098, 20), '29e1640082ce07c2f1bc68a1026000e3110668b1')
        self.assertEqual(code(0x8d4d6cde, 90),
                         '82ce088ca8a00000000d10600d60280620e100018608221082cec70169a00000e940f040b84d0850'
                         '0e30fe6707c40640026007c4020e00daff7fffff000000de02048c050000666c71c27820ad0501e8'
                         '0000084d04c408201000')
        step = 0x7fffffff // 200
        editor = [b * step + (step >> 1) for b in range(200)]
        self.assertEqual((editor[0], editor[-1]), (5368709, 2142114891))
        for value in defaults + editor:
            self.assertFalse(is_ready(value), hex(value))
        self.assertNotEqual(ready & 0xffff, 0)   # any value a preset restores has a low half of 0
        # A preset load: stock's preset import 8d4e35a8(preset, flag) has three callers,
        # each hooked so a padMix kit's loaded keys are set up (sdk_drum_preset_load). It
        # writes the track at the preset's +4 (8d4e22d4: R0 = [P5 + 0x4]) in that track's
        # current pattern (8d587708), which the hook reads back.
        from tooling.firmware import decode_branch
        sites = {0x8d4dda96: '00e3892d', 0x8d4ddb84: '00e3122d', 0x8d4dde44: '00e3b22b'}
        callers = set()
        for block in blocks:
            if block['fill'] or not block['stored']:
                continue
            body = stock[block['offset'] + 16:block['offset'] + 16 + block['stored']]
            for i in range(0, len(body) - 8, 2):
                address = block['target'] + i
                half = int.from_bytes(body[i:i + 2], 'little')
                if half >> 8 == 0xe3 and decode_branch(address, body[i:i + 4]) == (0x8d4e35a8, True):
                    callers.add(address)
                elif half == 0xde00 and body[i + 6:i + 8] == bytes(2) and \
                        (int.from_bytes(body[i + 2:i + 4], 'little') << 16 | int.from_bytes(body[i + 4:i + 6], 'little')) == 0x8d4e35a8:
                    callers.add(address)
        self.assertEqual(callers, set(sites))
        for address, before in sites.items():
            self.assertEqual(code(address, 4), before)
        hooks = {int(p['address'], 16): p for p in config()['patches'] if p.get('symbol') == 'sdk_drum_preset_load'}
        self.assertEqual({a: (p['before'], p['length'], p['call'], p['service']) for a, p in hooks.items()},
                         {a: (h, 4, True, 'drums') for a, h in sites.items()})
        self.assertEqual(decode_branch(0x8d4e35bc, bytes.fromhex(code(0x8d4e35bc, 4))), (0x8d4e22d4, True))
        self.assertEqual(decode_branch(0x8d4e35c0, bytes.fromhex(code(0x8d4e35c0, 4))), (0x8d587708, True))
        self.assertEqual(code(0x8d4e22e2, 2), '68a0')   # R0 = [P5 + 0x4]
