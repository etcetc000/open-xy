"""Aux module runtime boundaries, using production adapter sources."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_sample_services import HOST_CXX
ROOT=Path(__file__).resolve().parents[1]
PLATFORM=ROOT/'sdk/platform/opxy-1.2.1'

@unittest.skipUnless(HOST_CXX,'No working host C++ compiler')
class AuxRuntime(unittest.TestCase):
    def run_fixture(self,name):
        with tempfile.TemporaryDirectory(prefix='aux-runtime-') as temporary:
            folder=Path(temporary)
            stock=(PLATFORM/'stock.hpp').read_text().replace('typedef __UINTPTR_TYPE__ Address;','typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*','const volatile u32*','const volatile unsigned char*'):
                stock=stock.replace('reinterpret_cast<'+pointer+'>(address)','reinterpret_cast<'+pointer+'>(host_stock_address(address))')
            (folder/'stock.hpp').write_text(stock)
            for source in ('aux_modules.cpp','synth_controls.cpp','stock_browser.hpp'):
                (folder/source).write_bytes((PLATFORM/source).read_bytes())
            binary=folder/'fixture'
            compiled=subprocess.run([HOST_CXX,'-std=c++11','-O2','-Wall','-Wextra','-Werror', '-I'+str(folder),'-I'+str(ROOT/'sdk/include'),'-I'+str(PLATFORM),str(ROOT/'tests/fixtures'/f'{name}.cpp'),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
    def test_selection_and_removal_publish_project_change(self):
        self.run_fixture('aux_modules')

    def test_project_reload_preserves_instrument_values(self):
        self.run_fixture('aux_refresh')
