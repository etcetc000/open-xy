"""The idle cut: the project's idle threshold for drum sampler voices, SDK synths
and kits that settle, its firmware edit and its arithmetic."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.firmware import decode_branch
from tooling.pipeline import config
from test_sample_services import HOST_CXX
from test_divide import blackfin_compiler

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


class IdleCut(unittest.TestCase):
    def test_the_idle_capability_carries_the_voice_loop_hook(self):
        found = [p for p in config()['patches'] if p.get('symbol') == 'sdk_idle_render']
        self.assertEqual(len(found), 1)
        p = found[0]
        # The track's voice loop (8d51c708) calls the renderer chooser here.
        self.assertEqual((int(p['address'], 16), p['length'], p['call'], p['kind']), (0x8d51c7a6, 4, True, 'hook'))
        self.assertEqual(p['service'], 'idle')
        self.assertNotIn('profiling', p)
        self.assertEqual(decode_branch(0x8d51c7a6, bytes.fromhex(p['before'])), (0x8d51bbaa, True))
        self.assertIn('idle_cut', native.SDK_UNITS)
        self.assertEqual(native.UNIT_SERVICES['idle_cut'], 'idle')
        addresses = [p['address'] for p in config()['patches']]
        self.assertEqual(len(addresses), len(set(addresses)))
        # In a build with idle the adapters reach the idle cut's symbols only for an engine
        # that settles; without it they carry none of it (adapters.idle_values).
        from tooling.adapters import DRUM_IDLE, VOICE_IDLE
        for template, pieces in (('voice.cpp.in', VOICE_IDLE), ('drum_voice.cpp.in', DRUM_IDLE)):
            text = (PLATFORM / template).read_text(encoding='utf-8')
            self.assertNotIn('idle_cut', text, template)
            for key in pieces:
                text = text.replace('@' + key + '@', pieces[key])
            self.assertIn('IdleLinks<kSettles>::floor()', text, template)
            self.assertNotIn('sdk_idle_floor())', text.replace('return sdk_idle_floor(); }', ''), template)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_the_block_peak_is_the_saturating_abs_on_the_device(self):
        # Every block of a voice whose engine settles takes its peak: on the Blackfin
        # one saturating ABS a sample (-2^31 gives 2^31 - 1, as the portable path
        # the host tests run), with no branch in the loop.
        with tempfile.TemporaryDirectory(prefix='idle-peak-') as folder:
            source, listing = Path(folder) / 'peak.cpp', Path(folder) / 'peak.s'
            source.write_text('#include "idle_cut.hpp"\n'
                              'opxy::i32 block_peak(const opxy::i32* x) { return opxy::platform::idle_cut::peak(x, 64); }\n')
            subprocess.run([str(blackfin_compiler()), '-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding',
                            '-I' + str(PLATFORM), '-I' + str(ROOT / 'sdk/include'), '-S', str(source), '-o', str(listing)],
                           check=True, capture_output=True)
            text = listing.read_text().upper()
            self.assertIn('ABS', text)
            body = text[text.index('LSETUP'):] if 'LSETUP' in text else text
            loop = body[:body.index('RTS')]
            self.assertNotRegex(loop, r'IF !?CC JUMP')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_arithmetic_and_the_hook_on_the_host(self):
        with tempfile.TemporaryDirectory(prefix='idle-cut-') as folder:
            folder = Path(folder)
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            stock = stock.replace('reinterpret_cast<T*>(address)', 'reinterpret_cast<T*>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            source = folder / 'idle_cut.cpp'
            source.write_text((PLATFORM / 'idle_cut.cpp').read_text())
            binary = folder / ('idle_cut.exe' if os.name == 'nt' else 'idle_cut')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder), '-I' + str(PLATFORM),
                       '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/idle_cut.cpp'), str(source),
                       '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Two processes: the floor is probed once, without the setting and with it.
            for argument in ([], ['setting']):
                result = subprocess.run([str(binary), *argument], capture_output=True, text=True, timeout=120, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(result.stdout.strip(), 'ok', argument)


if __name__ == '__main__':
    unittest.main()
