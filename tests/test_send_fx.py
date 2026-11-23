"""Send-effect idling: the send hook skips an effect with nothing to play."""
import os
import re
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
PROCESS_SEND = 0x8d50f594


class SendFx(unittest.TestCase):
    def test_both_send_calls_reach_the_hook_with_the_idle_capability(self):
        hooks = [p for p in config()['patches'] if p.get('symbol') == 'sdk_send_fx']
        self.assertEqual([p['address'] for p in hooks], ['0x8d5116a0', '0x8d511908'])
        for p in hooks:
            # Each site is stock's own call of the send process; the hook replaces only it.
            self.assertEqual(decode_branch(int(p['address'], 16), bytes.fromhex(p['before'])), (PROCESS_SEND, True))
            self.assertTrue(p['call'])
            self.assertEqual(p['kind'], 'hook')
            self.assertEqual(p['service'], 'idle')
            self.assertNotIn('profiling', p)
        # Linked only with idle: nothing of it is in a build that does not ask for it.
        self.assertIn('send_fx', native.SDK_UNITS)
        self.assertEqual(native.UNIT_SERVICES['send_fx'], 'idle')

    def host_run(self, fixture):
        """Build tests/fixtures/<fixture> with send_fx.cpp on the host (stock's memory mapped by the fixture) and
        return what it prints."""
        with tempfile.TemporaryDirectory(prefix='send-fx-') as folder:
            folder = Path(folder)
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            stock = stock.replace('reinterpret_cast<T*>(address)', 'reinterpret_cast<T*>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            source = folder / 'send_fx.cpp'
            source.write_text((PLATFORM / 'send_fx.cpp').read_text())
            for name in ('send_bus.hpp', 'idle_cut.hpp'):
                (folder / name).write_text((PLATFORM / name).read_text())
            binary = folder / ('send_fx.exe' if os.name == 'nt' else 'send_fx')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            # send_fx_scan.cpp includes send_fx.cpp itself.
            units = [str(ROOT / 'tests/fixtures' / fixture)] + ([] if fixture == 'send_fx_scan.cpp' else [str(source)])
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                       '-I' + str(ROOT / 'sdk/include'), *units, '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=300, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout.strip()

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_a_send_idles_only_with_nothing_to_play_and_wakes_on_its_block(self):
        self.assertEqual(self.host_run('send_fx.cpp'), 'ok')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_silence_scan_decides_as_the_word_by_word_check(self):
        # The largest magnitude against the floor, on every block, as the check that stopped at the
        # first sample at or above it: one sample of every value from -2^17 to 2^17 at a rotating
        # position, the ends of the range (-2^31 included), random blocks around the floor of both
        # signs; quiet and loud blocks both occur.
        printed = self.host_run('send_fx_scan.cpp').split()
        self.assertEqual(printed[0], 'ok')
        blocks, quiet = int(printed[1]), int(printed[3])
        self.assertGreater(blocks, 900000)
        self.assertGreater(quiet, blocks // 20)
        self.assertGreater(blocks - quiet, blocks // 20)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_the_silence_scan_is_one_hardware_loop_of_two_instructions_a_word_on_the_device(self):
        # Each buffer: one load, then a hardware loop of count - 1 passes of [ABS with the next load, MAX],
        # then ABS and MAX of the last word: exactly `count` words read, no exit from the loop. Four scans:
        # the input before the call; the effect's left and right output and the result after it.
        with tempfile.TemporaryDirectory(prefix='send-fx-device-') as folder:
            listing = Path(folder) / 'send_fx.s'
            subprocess.run([str(blackfin_compiler()), '-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding',
                            '-fno-exceptions', '-fno-rtti', '-I' + str(PLATFORM), '-I' + str(ROOT / 'sdk/include'),
                            '-S', str(PLATFORM / 'send_fx.cpp'), '-o', str(listing)], check=True, capture_output=True)
            text = re.sub(r'//[^\n]*', '', listing.read_text())
        scan = re.compile(r'(R\d) = \[(P\d)\+\+\];\s*LSETUP \(1f, 2f\) LC0 = (P\d);\s*'
                          r'1:\s*(R\d) = ABS \1 \|\| \1 = \[\2\+\+\] \|\| NOP;\s*'
                          r'2:\s*(R\d) = MAX \(\5, \4\);\s*\4 = ABS \1;\s*\5 = MAX \(\5, \4\);')
        self.assertEqual(len(scan.findall(text)), 4, 'four scans of the form above')
        self.assertEqual(text.count('LSETUP (1f, 2f) LC0'), 4)
        # The loop counts: 127 (a bus block, 128 words) and 63 (64 words a channel).
        body = text[text.index('sdk_send_fx:'):]
        self.assertRegex(body, r'P\d = 127 \(X\);')
        self.assertRegex(body, r'P\d = 63 \(X\);')

if __name__ == '__main__':
    unittest.main()
