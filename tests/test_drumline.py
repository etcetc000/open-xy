"""Stock Drumline's second Shift row (the build option drumlineRow2, drumline.hpp): the
shared hooks and this option's own against host doubles, with the option and without
(M3's filter and filter envelope words stay stock's in both); the build option's wiring;
and the stock sites it relies on."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tooling import browser_kit, native, startup
from tooling.firmware import active_services, decode_branch, patch_active
from tooling.formats import ldr, mapped_offset, sha
from tooling.machines import BUILD_OPTIONS, read_collection
from machine_fixtures import FOLDER, fixture_entries
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
# The option's two sites: the drum renderer's load of a voice's method table,
# and the end of the dbox page's Shift branch.
SITES = {'0x8d51b554': ('sdk_dbox_render_select', '48914130', '0x1527b0'),
         '0x8d531192': ('sdk_drumline_page_hook', 'a0e4b400', '0x1683ee')}
OPTION = 'drumlineRow2'
# The services the option turns on: its own and the drum hooks it extends.
SERVICES = {'samples', 'octaves', 'drums', 'drum_sends', 'drum_locks', 'drumline'}


def run(command, **kwargs):
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    return subprocess.run(command, capture_output=True, text=True, **kwargs, **hidden)


def words(data):
    return [int.from_bytes(data[i:i + 2], 'little') for i in range(0, len(data), 2)]


class Drumline(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_hooks_with_the_option_and_stock_without_it(self):
        with tempfile.TemporaryDirectory(prefix='drumline-') as folder:
            folder = Path(folder)
            # Stock's addresses reach host doubles, as test_drum_services maps them.
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            for header in ('drum_values.hpp', 'drum_lock_store.hpp'):
                (folder / header).write_text((PLATFORM / header).read_text())
            # The dbox objects live at stock's addresses on the device; here, the fixture's.
            unit = (PLATFORM / 'drumline.cpp').read_text()
            index = 'dbox_index(static_cast<u32>(reinterpret_cast<Address>(self)))'
            self.assertEqual(unit.count(index), 1)
            unit = unit.replace(index, 'host_dbox_index(self)').replace(
                '#include "stock.hpp"\n', '#include "stock.hpp"\nopxy::u32 host_dbox_index(const unsigned char*);\n', 1)
            (folder / 'drumline.cpp').write_text(unit)
            sources = [str(folder / 'drumline.cpp')]
            for name in ('drum_locks.cpp', 'drum_machines.cpp', 'synth_controls.cpp', 'drum_synthesis_stock.cpp',
                         'drum_values_stock.cpp'):
                (folder / name).write_text((PLATFORM / name).read_text())
                sources.append(str(folder / name))
            for option, expected in ((True, 'render ok'), (False, 'stock behaviour ok')):
                with self.subTest(option=option):
                    binary = folder / (('on' if option else 'off') + ('.exe' if os.name == 'nt' else ''))
                    built = run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                                 '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                                 *(['-DDRUMLINE_ON'] if option else []),
                                 str(ROOT / 'tests/fixtures/drumline.cpp'), *sources, '-o', str(binary)], timeout=180)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    result = run([str(binary)], timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(expected, result.stdout)
                    self.assertIn('packing ok', result.stdout)

    def test_the_profile_reviews_two_sites_of_the_options_own_service(self):
        services = PROFILE['services']
        self.assertEqual(services['drumline'], [OPTION])
        for name in SERVICES - {'drumline'}:
            self.assertIn(OPTION, services[name], name)
        hooks = {p['address']: p for p in PROFILE['patches'] if p.get('service') == 'drumline'}
        self.assertEqual(set(hooks), set(SITES))
        stubs = (PLATFORM / 'drumline_hooks.s').read_text(encoding='utf-8')
        for address, (symbol, before, offset) in SITES.items():
            patch_ = hooks[address]
            self.assertEqual((patch_['kind'], patch_['symbol'], patch_['before'], patch_['call'], patch_['length'],
                              patch_['file_offset']), ('hook', symbol, before, True, 4, offset))
            self.assertIn('.global %s\n' % symbol, stubs)
        # Each stub replays what it replaces: P0 = [P1]; R0 = P1 (0x9148, 0x3041) ...
        self.assertEqual(words(bytes.fromhex(SITES['0x8d51b554'][1])), [0x9148, 0x3041])
        body = stubs[stubs.index('sdk_dbox_render_select:\n'):]
        body = body[:body.index('.size')]
        self.assertIn('    P0 = [P1];\n', body)
        self.assertIn('    R0 = P1;\n    R3 = [SP++];\n    ASTAT = [SP++];\n    RTS;\n', body)
        self.assertIn('R3.H = 0x8404; R3.L = 0xce3c;', body)
        self.assertTrue(body.startswith('sdk_dbox_render_select:\n    [--SP] = ASTAT;\n    [--SP] = R3;\n'))
        # ... and R0 = B[P4 + 0xb4] (Z) after the C++ (a0e4 b400).
        body = stubs[stubs.index('sdk_drumline_page_hook:\n'):]
        body = body[:body.index('.size')]
        self.assertIn('    R0 = P4;\n    CALL sdk_drumline_page;\n    HOOK_RESTORE\n    R0 = B[P4 + 0xb4] (Z);\n    RTS;\n',
                      body)
        self.assertIn('0x11a06d28', (PLATFORM / 'drumline.cpp').read_text(encoding='utf-8'))
        self.assertEqual(native.UNIT_SERVICES['drumline'], 'drumline')
        self.assertEqual(native.UNIT_SERVICES['drumline_hooks'], 'drumline')

    def test_the_option_is_off_unless_a_collection_names_it(self):
        self.assertIn(OPTION, BUILD_OPTIONS)
        for kinds in (['synth'], ['drum'], ['drum', 'padSend', 'padMix', 'hidePages'], ['sample', 'extension']):
            services = active_services(PROFILE, kinds)
            self.assertNotIn('drumline', services)
            for patch_ in PROFILE['patches']:
                if patch_.get('service') == 'drumline':
                    self.assertFalse(patch_active(patch_, services))
        self.assertEqual(active_services(PROFILE, ['synth', OPTION]), SERVICES)
        # The option reaches a collection from its file or the command line, each once.
        with tempfile.TemporaryDirectory() as folder:
            listing = Path(folder) / 'collection.json'
            listing.write_text(json.dumps({'schema': 1, 'machines': [{'path': str(FOLDER / 'probesynth')}],
                                           'options': [OPTION]}))
            self.assertEqual(read_collection(listing)[-1], {'option': OPTION})
            self.assertEqual(read_collection(listing, (), (), [OPTION]).count({'option': OPTION}), 1)
            listing.write_text(json.dumps({'schema': 1, 'machines': [], 'options': ['drumline']}))
            with self.assertRaises(ValueError):
                read_collection(listing)
        with self.assertRaises(ValueError):
            read_collection(None, [FOLDER / 'probesynth'], (), ['padMix'])
        entries = fixture_entries('probesynth')
        _profile, plain, _definitions = native.configuration(entries)
        _profile, optioned, _definitions = native.configuration(entries + [{'option': OPTION}])
        self.assertEqual((native.options_of(plain), native.options_of(optioned)), ((), (OPTION,)))
        self.assertNotIn(OPTION, native.service_users(plain))
        self.assertIn(OPTION, native.service_users(optioned))
        units = lambda machines: [step for _name, m, step in native.plan(machines) if m is None]
        self.assertNotIn('drumline', units(plain))
        self.assertIn('drumline', units(optioned))
        self.assertIn('drumline_hooks', units(optioned))
        # The package records it (its collection), and so does the provenance hash.
        self.assertNotEqual(native.provenance(plain)['collection'], native.provenance(optioned)['collection'])
        args = native.argparse.ArgumentParser()
        native.add_collection_arguments(args)
        parsed = args.parse_args(['--machine', str(FOLDER / 'probesynth'), '--option', OPTION])
        self.assertEqual(native.selected_collection(parsed)[-1], {'option': OPTION})
        # The generated startup names the service only with the option.
        with_option = startup.render(['probesynth'], drumline=True)
        without = startup.render(['probesynth'])
        self.assertIn('const void* const sdk_drumline = sdk_drumline_service;', with_option)
        self.assertIn('const void* const sdk_drumline = nullptr;', without)
        self.assertNotIn('sdk_drumline_service', without)
        # A browser kit builds no build options.
        with patch.object(native, 'package_collection', return_value=entries + [{'option': OPTION}]):
            with self.assertRaisesRegex(ValueError, 'without build options'):
                browser_kit.export(Path('unused'), Path('unused'), {}, stock_catalog=b'')

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_sites_hold_what_the_option_relies_on(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), PROFILE['stock_sha256'])
        image = ldr(stock)

        def at(address, length):
            offset = mapped_offset(image, address, length)
            return stock[offset:offset + length]

        def load(address):
            # Blackfin+ `reg = imm32`: 0xdaNN (NN 0..7 R0..R7, 8..13 P0..P5), high half, low half.
            w = words(at(address, 8))
            self.assertEqual(w[0] & 0xff00, 0xda00, hex(address))
            return w[0] & 0xff, w[1] << 16 | w[2]

        for address, (symbol, before, offset) in SITES.items():
            self.assertEqual(mapped_offset(image, int(address, 16), 4), int(offset, 16))
            self.assertEqual(at(int(address, 16), 4).hex(), before)

        # The drum renderer's machine call: P1 = [P4 + 0x94] (8d51b53e), R2 = [P4 + 0x10]
        # and R3 = [FP + 0xc] (8d51b542), [SP + 0xc] = R3 (8d51b54a), R1 = 0x11b01948,
        # then the hooked pair, P0 = [P0 + 0xc] and CALL (P0).
        self.assertEqual(at(0x8d51b53e, 4).hex(), '21e52500')
        self.assertEqual(at(0x8d51b542, 8).hex(), '03c80018fba022a1')
        self.assertEqual(at(0x8d51b54a, 2).hex(), 'f3b0')
        self.assertEqual(load(0x8d51b54c), (1, 0x11b01948))
        self.assertEqual(at(0x8d51b558, 4).hex(), 'c0ac6000')
        # The dbox method table: +12 the render 11a06d28, +16 the trigger, +20 note start.
        table = [int.from_bytes(at(0x8404ce3c + 4 * i, 4), 'little') for i in range(6)]
        self.assertEqual(table[3:], [0x11a06d28, 0x8d513bea, 0x8d513a36])
        # Its factory: 0x84c50b9c + index * 0x230 (8d513d1c..8d513d2e); the ctor sets the table (8d513aa6).
        self.assertEqual(at(0x8d513d1c, 4).hex(), '21e13002')
        self.assertEqual(load(0x8d513d26), (1, 0x84c50b9c))
        self.assertEqual(load(0x8d513aa6), (1, 0x8404ce3c))
        # The trigger zeroes the block count +0x30 (R0 = 0 at 8d513bf4, [P1 + 0x30] = R0 at
        # 8d513c0e); the DSP counts each block (11a06fa6..11a06faa).
        self.assertEqual(at(0x8d513bf4, 2).hex(), '0060')
        self.assertEqual(at(0x8d513c0e, 2).hex(), '08b3')
        self.assertEqual(at(0x11a06fa6, 6).hex(), '28a3086428b3')
        # The DSP decodes word 0 as ((w >>> 8) * 66) >>> 23 (11a06d62, 11a06d6a, 11a06d80)
        # and squares word 6 (R7 = [P0 + 0x18], 11a06db8; R6 = R7 * R7, 11a06dce).
        self.assertEqual(at(0x11a06d62, 4).hex(), '82cec307')
        self.assertEqual(at(0x11a06d6a, 4).hex(), '24e14200')
        self.assertEqual(at(0x11a06d80, 4).hex(), '82ce4b01')
        self.assertEqual(at(0x11a06db8, 8).hex(), '31ca242187a10000')
        self.assertEqual(at(0x11a06dce, 4).hex(), '31cabf21')
        # It reads the region only through [FP - 0x10] (11a06d36) and applies level, word 7,
        # at the end (11a073e6, 11a073f8), before the renderer pans the block.
        self.assertEqual(at(0x11a073e6, 2).hex(), 'c9b9')
        self.assertEqual(at(0x11a073f8, 2).hex(), 'c8a1')

        # The dbox page's Shift branch (8d530fb8): its six texts through 8d6011f0, the
        # value nodes from P4 + 0x44 (P2 = 0x44, 8d531040), the first title [P4 + 0x54]
        # (8d531048), the play-mode icons from P4 + 0x6c (8d53101e) shown by B[node + 0x5c]
        # (8d531038), and its strings.
        for call in (0x8d53105a, 0x8d531072, 0x8d531082, 0x8d5310c2, 0x8d531158, 0x8d53118a):
            self.assertEqual(decode_branch(call, at(call, 4)), (0x8d6011f0, True))
        self.assertEqual(decode_branch(0x8d530fb8, at(0x8d530fb8, 4)), (0x8d51f184, True))
        self.assertEqual(at(0x8d531040, 4).hex(), '2ae14400')
        self.assertEqual(at(0x8d531048, 4).hex(), '20e41500')
        self.assertEqual(at(0x8d53101e, 4).hex(), '29e16c00')
        self.assertEqual(at(0x8d531038, 4).hex(), '82e65c00')
        self.assertEqual(at(0x84054890, 0x1b), b'%02d\0\0\0\0random\0\0pan\0volume\0')
        # Its numbers: random (w >>> 8) * 100 >>> 23, pan (p + 100) * 99 / 200 and volume
        # (v + 30) * 99 / 50 through 0x51eb851f, each within 0..99.
        self.assertEqual(at(0x8d531098, 6).hex(), '404de840b84d')
        self.assertEqual(load(0x8d5310cc), (0, 0x51eb851f))
        self.assertEqual(at(0x8d5310e4, 2).hex(), 'f264')        # R2 += 30

        # Stock's Shift-press action for engine 4 acts on encoder 1 only (8c0b2752..8c0b275a).
        # R0 = 4; CC = R7 == R0; IF !CC JUMP; CC = R5 == R7; IF CC JUMP 8c0b27f8 (the mode cycle 8d4d6180).
        self.assertEqual(at(0x8c0b2752, 10).hex(), '2060070803103d084f18')
        # The builder publishes with the pad's slot: R2 = B[P5] (8d5ad766), which the
        # region lookup wrote (R2 = P5, 8d5ad680; B[P5] = R1, 8d589308), the record at
        # the track object + 0x394c + slot * 0x80 (8d589308..8d58931e).
        self.assertEqual(at(0x8d5ad766, 8).hex(), '82ce04c2f3bc2a99')
        self.assertEqual(at(0x8d5ad680, 2).hex(), '5530')
        self.assertEqual(decode_branch(0x8d5ad684, at(0x8d5ad684, 4)), (0x8d58926c, True))
        self.assertEqual(at(0x8d589308, 8).hex(), '82ce3982299b0000')
        self.assertEqual(at(0x8d589318, 4).hex(), '22e14c39')
        # The per-key editor (8d4d5f60) finds the pad and never touches +0x00: it edits +0x04
        # (8d4d5fbc) and the rest through its table at 0x84039070.
        self.assertEqual(load(0x8d4d5f94), (10, 0x84039070))
        self.assertEqual(at(0x8d4d5fbc, 2).hex(), '68b0')


if __name__ == '__main__':
    unittest.main()
