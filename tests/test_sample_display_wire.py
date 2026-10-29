import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_sample_services import HOST_CXX

class SampleDisplayWireTest(unittest.TestCase):
    def test_complete_generation_bound_packets(self):
        self.run_fixture('sample_display_wire')

    def test_real_backend_bounded_work_and_lifetime(self):
        self.run_fixture('sample_display_telemetry')

    def test_unavailable_host_provider(self):
        self.run_fixture('sample_display_stub')

    def run_fixture(self, fixture):
        if not HOST_CXX:
            self.fail("A host C++ compiler is required for sample telemetry tests")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'sample-display'
            subprocess.run([HOST_CXX, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-D__builtin_bfin_ssync()=host_barrier()',
                            '-I' + str(root / 'sdk/include'),
                            '-I' + str(root / 'sdk/platform/opxy-1.2.1'),
                            str(root / ('tests/fixtures/' + fixture + '.cpp')), '-o', str(output)], check=True)
            subprocess.run([str(output)], check=True)
