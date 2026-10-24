"""Bounded note unlinking preserves stable pointers and rejects malformed state."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class RecordingContainer(unittest.TestCase):
    def test_selective_unlink_and_atomic_rejection(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='recording-container-') as folder:
            binary = Path(folder) / ('fixture.exe' if os.name == 'nt' else 'fixture')
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'sdk/include'),
                       '-I' + str(ROOT / 'sdk/platform/opxy-1.2.0'),
                       str(ROOT / 'tests/fixtures/recording_container.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
