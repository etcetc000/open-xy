"""Module builds: opt-in, and MTP admits module files beside stock's project rules."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import module_runtime, native
from tooling.pipeline import ROOT, config
from tooling.startup import render


class ModuleLoaderStock(unittest.TestCase):
    def test_loading_is_opt_in_and_runs_before_registration(self):
        ordinary = render(['example'])
        enabled = render(['example'], modules=True)
        self.assertNotIn('sdk_module_load', ordinary)
        self.assertLess(enabled.index('    sdk_module_load();'), enabled.index('    example_register();'))
        machines = native.Collection(modules=True)
        self.assertTrue(native.subset(machines, []).modules)
        plain = {name for name, _, _ in native.plan(native.Collection())}
        loading = {name for name, _, _ in native.plan(machines)}
        self.assertEqual(loading - plain, {'sdk/module_loader.o', 'sdk/module_loader_stock.o', 'sdk/safe_mode.o',
                                           'sdk/module_mtp.o',
                                           'sdk/module_cache.o', 'sdk/module_synths.o', 'sdk/synth_pool.o'})

    def test_profile_adds_only_the_module_hooks(self):
        base = config()
        added = module_runtime.profile(base)['patches'][len(base['patches']):]
        self.assertEqual([p['symbol'] for p in added],
                         ['sdk_module_mtp_folder_match', 'sdk_module_mtp_match', 'sdk_module_asset_load',
                          'sdk_module_catalog_load', 'sdk_module_preset_dir_open', 'sdk_module_preset_dir_next',
                          'sdk_module_preset_dir_close', 'sdk_module_preset_load', 'sdk_module_board_receive'])

    def test_native_mtp_matcher_admits_module_files_only_in_the_module_folder(self):
        compiler = os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / 'stock.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
Address host_stock_address(u32);
template<class Function> Function* stock(u32 address) {
    return reinterpret_cast<Function*>(host_stock_address(address));
}
}}
''')
            # A copy, so its "stock.hpp" is the double above, not the real one beside it.
            source = directory / 'module_mtp.cpp'
            shutil.copyfile(ROOT / 'sdk/platform/opxy-1.2.1/module_mtp.cpp', source)
            runner = directory / ('matcher.exe' if os.name == 'nt' else 'matcher')
            subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(directory), '-I' + str(ROOT / 'sdk/include'),
                            str(ROOT / 'tests/fixtures/module_mtp.cpp'),
                            str(source), '-o', str(runner)],
                           check=True, capture_output=True, text=True)

            def match(path, kind='file', other_rule=False):
                return subprocess.check_output([str(runner), path, kind, str(int(other_rule))], text=True).strip() == '1'
            for path in ('/open-xy/modules/test.opx', '/open-xy/modules/my-synth_2.opx', '/projects/folder/song.xy'):
                self.assertTrue(match(path), path)
            for path in ('/open-xy/test.opx', '/open-xy/gui-module.oxc', '/projects/openxy/test.opx', '/open-xy/modules/.opx',
                         '/open-xy/modules/a b.opx', '/open-xy/modules/a.opx/extra', '/open-xy/modules/a.opxx',
                         '/open-xy/modules/../a.opx', '/open-xy/modules/x.oxc', '/test.opx'):
                self.assertFalse(match(path), path)
            self.assertFalse(match('/open-xy/modules/test.opx', other_rule=True))
            # Folders: only the two of ours beside stock's, and only on the rule they ride on.
            for path in ('/open-xy', '/open-xy/modules', '/projects'):
                self.assertTrue(match(path, 'folder'), path)
            for path in ('/open-xy/other', '/open-xy/modules/sub', '/openxy', '/open-xy/', '/samples'):
                self.assertFalse(match(path, 'folder'), path)
            self.assertFalse(match('/open-xy', 'folder', other_rule=True))


if __name__ == '__main__': unittest.main()
