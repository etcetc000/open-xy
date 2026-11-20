"""The intensity rule and the example META punch-in effects, compiled for the host."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sample_services import HOST_CXX, ROOT

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
RECORD_CHECK = 'static_assert(sizeof(Animation) == 12, "Stock animation record");'


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class PunchMeta(unittest.TestCase):
    def test_intensity_throws_gates_roll_kill_reverse(self):
        with tempfile.TemporaryDirectory(prefix='punch-meta-') as directory:
            folder = Path(directory)
            # The animation record is three 32-bit words on the device; a
            # 64-bit host has wider pointers, so its copy drops that check.
            header = (ROOT / 'sdk/include/opxy/punch.hpp').read_text()
            self.assertIn(RECORD_CHECK, header)
            (folder / 'opxy').mkdir()
            (folder / 'opxy/punch.hpp').write_text(header.replace(RECORD_CHECK, ''))
            binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                       '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                       str(ROOT / 'tests/fixtures/punch_meta.cpp'), str(PLATFORM / 'punch_example.cpp'),
                       '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
