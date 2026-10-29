"""Bounce IPC byte format, endpoint direction and caller-owned sessions."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.formats import require
from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerBounceWireTests(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_wire_contract(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-bounce-wire-') as directory:
            binary = Path(directory) / ('wire.exe' if os.name == 'nt' else 'wire')
            built = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                str(ROOT / 'tests/fixtures/arranger_bounce_wire.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            require(built.returncode == 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            require(ran.returncode == 0, ran.stdout + ran.stderr)
