"""The output slack's pure parts (output_slack.hpp) on the host: the quiet check, the
half index, arming and one wrapper call's delivery."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]


class OutputSlack(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_quiet_arming_and_delivery(self):
        with tempfile.TemporaryDirectory(prefix='output-slack-') as folder:
            binary = Path(folder) / ('slack.exe' if os.name == 'nt' else 'slack')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                       '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'), str(ROOT / 'tests/fixtures/output_slack.cpp'),
                       '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), 'ok')

    def test_it_arms_about_four_seconds_after_boot(self):
        # kArmBlocks is what the host test's arming counts; this pins its value to the documented wait (about 4 s
        # of 64-frame blocks at 44.1 kHz), which the counting test alone would follow to any value.
        import re
        text = (ROOT / 'sdk/platform/opxy-1.2.1/output_slack.hpp').read_text(encoding='utf-8')
        blocks = int(re.search(r'^const u32 kArmBlocks = (\d+);$', text, re.M)[1])
        self.assertAlmostEqual(blocks * 64 / 44100, 4.0, delta=64 / 44100)


if __name__ == '__main__':
    unittest.main()
