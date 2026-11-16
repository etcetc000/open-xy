"""Retained resource admission and cleanup before executable code is touched."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT


class Resources(unittest.TestCase):
    def test_cache_survives_file_deletion_and_releases_every_failed_allocation(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            runner=Path(temp)/('resources.exe' if os.name=='nt' else 'resources')
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
            subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                            '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                            str(ROOT/'tests/fixtures/collection_resources.cpp'),
                            *[str(ROOT/'sdk/platform'/name) for name in (
                                'collection_resources.cpp','collection_activation.cpp','collection_probe.cpp')],
                            '-o',str(runner)],check=True,capture_output=True)
            expected={'normal':0,'invalid':1,'missing':2,'io-error':3,'short':4,'trailing':4,
                      'checksum':5,'oom1':6,'oom2':6,'oom3':6,'nul':7}
            for mode,status in expected.items():
                with self.subTest(mode=mode):
                    self.assertEqual(int(subprocess.check_output([str(runner),mode],text=True)),status)


if __name__=='__main__': unittest.main()
