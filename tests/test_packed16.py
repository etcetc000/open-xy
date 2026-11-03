"""Public packed arithmetic contracts, independent of machines and firmware."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class PackedProducts(unittest.TestCase):
    def test_signed_dot_saturation_and_lane_packing(self):
        with tempfile.TemporaryDirectory(prefix='packed16-') as folder:
            binary = Path(folder) / ('packed16.exe' if os.name == 'nt' else 'packed16')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                       '-I' + str(ROOT / 'sdk/include'),
                       str(ROOT / 'tests/fixtures/packed16.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
