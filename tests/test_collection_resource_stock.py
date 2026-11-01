"""Native resource adapters preserve stock ownership and canonical cached bytes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT


class NativeResources(unittest.TestCase):
    def test_baseline_forwarding_protected_svg_fallback_and_transactional_catalog(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);runner=folder/('resources.exe' if os.name=='nt' else 'resources')
            (folder/'stock.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
Address host_stock_address(u32);
template<class Function> Function* stock(u32 address) {
return reinterpret_cast<Function*>(host_stock_address(address));
}
}}
''')
            adapter=folder/'collection_resource_stock.cpp'
            adapter.write_bytes((ROOT/'sdk/platform/opxy-1.2.1/collection_resource_stock.cpp').read_bytes())
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
            subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                            '-I'+str(folder),'-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                            '-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(ROOT/'tests/fixtures/collection_resource_stock.cpp'),
                            str(adapter),'-o',str(runner)],check=True,capture_output=True)
            subprocess.run([str(runner)],check=True)


if __name__=='__main__':unittest.main()
