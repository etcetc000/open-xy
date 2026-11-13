import argparse
import ast
import contextlib
import io
import json
import tempfile
from pathlib import Path
import unittest

from tooling.checks import FIXTURES, fixture_environment, main, run_suite
from tooling import native
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class Sources(unittest.TestCase):
    def test_sdk_checks_use_require_never_assert(self):
        # python -O strips assert, and checks run once, under normal Python.
        files = [ROOT / 'sdk.py', *(ROOT / 'tooling').rglob('*.py'), *(ROOT / 'tools').glob('*.py')]
        found = [f'{path.relative_to(ROOT)}:{node.lineno}' for path in files
                 for node in ast.walk(ast.parse(path.read_text(encoding='utf-8'))) if isinstance(node, ast.Assert)]
        self.assertEqual(found, [], 'use tooling.formats.require instead of assert')


class CheckRunnerTests(unittest.TestCase):
    def options(self, **values):
        return argparse.Namespace(**{key: values.get(key) for key in (*FIXTURES, 'toolchain', 'compiler')})

    def test_public_mode_does_not_silently_use_stale_private_fixtures(self):
        env, native = fixture_environment(self.options(), {'PATH': 'retained', 'OPXY_TEST_NATIVE_ELF': 'stale',
                                                           'OPXY_TEST_COMPILER': 'stale'})
        self.assertEqual(env, {'PATH': 'retained'})
        self.assertFalse(native)

    def test_partial_native_inputs_fail_instead_of_falling_back_to_public(self):
        with self.assertRaises(ValueError):
            fixture_environment(self.options(native_package=Path('missing')), {})

    def test_native_inputs_must_exist_and_bind_elf_to_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            values = {}
            for key, (_env, relative) in FIXTURES.items():
                values[key] = root / key
                file = values[key] / relative if relative else values[key]
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(b'fixture')
            values['toolchain'] = root / 'tools'
            values['toolchain'].mkdir()
            env, native = fixture_environment(self.options(**values), {})
            self.assertTrue(native)
            # Hosted Windows runners can expose TEMP through an 8.3 alias.
            self.assertEqual(Path(env['OPXY_TEST_NATIVE_ELF']),
                             (values['native_package'] / 'analysis/module.elf').resolve())
            self.assertEqual(Path(env['OPXY_TEST_TOOLCHAIN']), values['toolchain'].resolve())
            self.assertNotIn('OPXY_TEST_COMPILER', env)
            # The compiler qualification was given reaches the tests that compile for the device.
            env, _native = fixture_environment(self.options(**values, compiler=root / 'compiler'), {})
            self.assertEqual(Path(env['OPXY_TEST_COMPILER']), (root / 'compiler').resolve())
            values['stock_os'].unlink()
            with self.assertRaises(ValueError):
                fixture_environment(self.options(**values), {})

    def test_native_suite_rejects_skipped_test_even_when_unittest_succeeds(self):
        with patch('tooling.checks.unittest.TestLoader.discover'), patch('builtins.print'), \
             patch('tooling.checks.unittest.TextTestRunner') as runner:
            result = runner.return_value.run.return_value
            result.wasSuccessful.return_value = True
            result.skipped = [('case', 'missing input')]
            self.assertEqual(run_suite(require_all=True), 1)
            self.assertEqual(run_suite(require_all=False), 0)

    def test_local_checks_require_native_inputs_instead_of_running_public_only(self):
        with patch('tooling.checks.subprocess.run') as run, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(['--local']), 1)
            run.assert_not_called()

    def test_local_checks_forward_local_rebuild_and_still_require_every_fixture(self):
        args = ['--local', '--native-package', '/package', '--stock-os', '/stock/os.ldr',
                '--stock-content', '/stock/content', '--toolchain', '/tools', '--compiler', '/compiler']
        with patch('tooling.checks.fixture_environment', return_value=({}, True)), \
             patch('tooling.checks.resolve_toolchain', return_value=(Path('/tools'), Path('/compiler'))), \
             patch('tooling.checks.require_release_host') as release, \
             patch('tooling.checks.subprocess.run') as run, contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(main(args), 0)
            release.assert_not_called()
            commands = [call.args[0] for call in run.call_args_list]
            self.assertTrue(any('verify' in command and '--local' in command for command in commands))
            self.assertTrue(any('--require-all' in command for command in commands))
            self.assertIn('Release qualification was not performed', output.getvalue())

    def test_native_checks_keep_release_guard_by_default(self):
        args = ['--native-package', '/package', '--stock-os', '/stock/os.ldr',
                '--stock-content', '/stock/content', '--toolchain', '/tools', '--compiler', '/compiler']
        with patch('tooling.checks.fixture_environment', return_value=({}, True)), \
             patch('tooling.checks.resolve_toolchain', return_value=(Path('/tools'), Path('/compiler'))), \
             patch('tooling.checks.require_release_host', side_effect=ValueError('release only')) as release, \
             patch('tooling.checks.subprocess.run') as run, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(args), 1)
            release.assert_called_once()
            run.assert_not_called()

    def test_local_verify_rejects_a_foreign_host_before_building(self):
        with tempfile.TemporaryDirectory() as temp:
            package = Path(temp)
            (package / 'analysis').mkdir()
            (package / 'analysis/compile-report.json').write_text(json.dumps({'host': 'another-host'}))
            with patch.object(native, 'host_platform', return_value='darwin-arm64'), \
                 patch.object(native, 'require_release_host') as release, \
                 patch.object(native, 'verification') as verify, contextlib.redirect_stderr(io.StringIO()) as error:
                self.assertEqual(native.main(['--local', '--package', str(package), '--stock-os', 'unused'], 'verify'), 1)
                self.assertIn('requires a package built on darwin-arm64', error.getvalue())
                release.assert_not_called()
                verify.assert_not_called()

    def test_verify_keeps_release_guard_without_local_option(self):
        with patch.object(native, 'require_release_host', side_effect=ValueError('release only')) as release, \
             patch.object(native, 'verification') as verify, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(native.main(['--package', 'unused', '--stock-os', 'unused'], 'verify'), 1)
            release.assert_called_once()
            verify.assert_not_called()
