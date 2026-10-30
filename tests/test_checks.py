import argparse
import ast
import tempfile
from pathlib import Path
import unittest

from tooling.checks import FIXTURES, fixture_environment, run_suite
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
        return argparse.Namespace(**{key: values.get(key) for key in (*FIXTURES, 'toolchain')})

    def test_public_mode_does_not_silently_use_stale_private_fixtures(self):
        env, native = fixture_environment(self.options(), {'PATH': 'retained', 'OPXY_TEST_NATIVE_ELF': 'stale'})
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
