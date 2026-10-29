"""Native bounce job ownership, cancellation, and queue admission."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerBounceJobTests(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_worker_gui_lifecycle(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-bounce-job-') as directory:
            binary = Path(directory) / ('transport.exe' if os.name == 'nt' else 'transport')
            built = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-D__builtin_bfin_ssync()=host_barrier()', '-I' + str(ROOT / 'sdk/include'),
                '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                str(ROOT / 'tests/fixtures/arranger_bounce_job.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
