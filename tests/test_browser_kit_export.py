"""The browser builder export (`sdk.py kit`) run end to end: the native package, the kit's objects and
fallbacks, the subset check against GNU ld (tools/check_browser_kit.mjs) and the builder page: without it a
broken export could pass every other test, since none runs it past its refusals."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from off_path_code import FIXTURES, kit_folder, player_folder, toolchain
from tooling.pipeline import ROOT


def stock_os():
    path = os.environ.get('OPXY_TEST_STOCK_OS')
    return Path(path) if path and Path(path).is_file() else None


@unittest.skipUnless(toolchain() and stock_os(), 'needs the pinned Blackfin tools (OPXY_TEST_COMPILER) and the '
                                                 'stock OS (OPXY_TEST_STOCK_OS)')
class BrowserKitExport(unittest.TestCase):
    def export(self, *machines):
        with tempfile.TemporaryDirectory(prefix='kit-export-') as work:
            out = Path(work) / 'kit'
            command = [sys.executable, str(ROOT / 'sdk.py'), 'kit', '--personal', '--stock-os', str(stock_os()),
                       '--out', str(out), '--toolchain', str(toolchain() / 'bin'), '--compiler', str(toolchain()),
                       '--host-cxx', os.environ.get('OPXY_TEST_HOST_CXX', 'g++'),
                       *(a for m in machines for a in ('--machine', str(m)))]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
            self.assertTrue((out / 'opxy-builder.html').is_file(), 'no builder page written')
            self.assertTrue((out / 'kit.json').is_file())

    def test_a_public_kit_collection_exports(self):
        with tempfile.TemporaryDirectory(prefix='kit-src-') as folder:
            kit = Path(folder) / 'probekit'
            kit_folder(kit, {'padMix': 1})
            self.export(kit, FIXTURES / 'probesynth')

    def test_sdk_1_25_machines_export_beside_older_ones(self):
        # A player reading Controls.instrument and a modelEdit kit, beside an older player and kit:
        # the kit's subset check links every selection of them (tools/check_browser_kit.mjs), so
        # the builder builds each alone or with the others from the one set of objects.
        with tempfile.TemporaryDirectory(prefix='kit-src-') as folder:
            player = Path(folder) / 'probeplayer'
            player_folder(player)
            self.export(FIXTURES / 'probepads', FIXTURES / 'probekit25', FIXTURES / 'probekit21', player)


if __name__ == '__main__':
    unittest.main()
