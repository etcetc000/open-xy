"""Cache qualification is isolated from baseline and candidate code."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT


class CacheQualification(unittest.TestCase):
    def test_only_a_known_resident_scratch_line_can_be_modified_and_it_is_restored(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            runner=Path(temp)/('cache.exe' if os.name=='nt' else 'cache')
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
            subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                            '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                            str(ROOT/'tests/fixtures/collection_cache.cpp'),
                            str(ROOT/'sdk/platform/collection_cache.cpp'),'-o',str(runner)],
                           check=True,capture_output=True)
            subprocess.run([str(runner)],check=True)


if __name__=='__main__': unittest.main()
