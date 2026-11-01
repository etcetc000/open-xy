"""Actual native coordinator under deterministic task/processor scheduling."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling import collection_activation as activation, collection_kernel as kernel
from tooling.pipeline import ROOT, config


class Coordinator(unittest.TestCase):
    def test_both_initializer_paths_cleanup_commit_verify_and_receipts(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);runner=folder/('kernel.exe' if os.name=='nt' else 'kernel')
            (folder/'stock.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
Address host_stock_address(u32);u32 word(u32);
template<class Function> Function* stock(u32 address) {
return reinterpret_cast<Function*>(host_stock_address(address));
}
}}
''')
            abi=activation.interface(config(),{'samples','drums','extensions'},True,0xa000)
            kernel.generate(folder,abi,bytes(range(32)),activation.fingerprint(abi))
            sources=['collection_gate','collection_handoff','collection_activation','collection_probe',
                     'collection_cache','collection_resources']
            native=['collection_io','collection_resource_stock']
            for name in native:
                (folder/(name+'.cpp')).write_bytes((ROOT/'sdk/platform/opxy-1.2.1'/(name+'.cpp')).read_bytes())
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
            result=subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                '-I'+str(folder),'-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                '-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(ROOT/'tests/fixtures/collection_kernel.cpp'),
                *(str(ROOT/'sdk/platform'/(s+'.cpp')) for s in sources),
                *(str(folder/(s+'.cpp')) for s in native),'-o',str(runner)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for mode in ('create-failure','ineligible','small-timeout','precommit-failure','bulk-stall',
                         'commit-failure','publish','verify-only','audio-publish','audio-verify'):
                with self.subTest(mode=mode):subprocess.run([str(runner),mode],check=True)

    def test_native_geometry_is_pinned_to_supported_heap(self):
        abi=activation.interface(config(),set(),True,0xa000)
        text=kernel.render(abi,bytes(32),bytes(32))
        self.assertNotIn('@',text)
        abi['geometry']['resident_base']+=32
        with self.assertRaises(ValueError):kernel.render(abi,bytes(32),bytes(32))


if __name__=='__main__':unittest.main()
