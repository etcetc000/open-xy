"""Native file/heap adapters: publication is gated by flush and exact readback."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT


class NativeStorage(unittest.TestCase):
    def test_checked_flush_readback_errors_and_nonfatal_heap(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);runner=folder/('io.exe' if os.name=='nt' else 'io')
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
            # Compile a copy so the quoted stock header resolves to this mock,
            # while the exact platform adapter source remains under test.
            adapter=folder/'collection_io.cpp'
            adapter.write_bytes((ROOT/'sdk/platform/opxy-1.2.1/collection_io.cpp').read_bytes())
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
            subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                            '-I'+str(folder),'-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                            '-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(ROOT/'tests/fixtures/collection_io.cpp'),
                            str(adapter),str(ROOT/'sdk/platform/collection_activation.cpp'),
                            str(ROOT/'sdk/platform/collection_probe.cpp'),'-o',str(runner)],check=True,capture_output=True)
            for mode in ('normal','invalid','audio','open-failed','write-short','write-error','close-error',
                         'flush-error','corrupt','trailing','short','read-error','heap','heap-oom','heap-overlap',
                         'heap-uninitialized','heap-try','heap-busy','heap-low','heap-unlock-error',
                         'abort-before','abort-open','abort-write','abort-close','abort-flush','abort-read'):
                with self.subTest(mode=mode):subprocess.run([str(runner),mode],check=True)


if __name__=='__main__':unittest.main()
