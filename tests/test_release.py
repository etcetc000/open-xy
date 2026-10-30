"""The site release: each deployment comes from its own branch."""
from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tooling import release


class Release(unittest.TestCase):
    def test_deployments_come_from_their_own_branch(self):
        self.assertEqual({target: source for target, (source, _options) in release.TARGETS.items()},
                         {'beta': 'staging', 'production': 'main'})
        with tempfile.TemporaryDirectory() as directory:
            for target, branch in (('beta', 'main'), ('production', 'staging'), ('beta', 'sdk/feature')):
                with self.subTest(target=target, branch=branch), mock.patch.object(release, 'branch', return_value=branch), \
                        mock.patch.object(release, 'run') as run, redirect_stderr(io.StringIO()) as error:
                    code = release.main(['--stock-os', 'os.ldr', '--stock-content', 'content', '--deploy', target,
                                         '--out', str(Path(directory) / target)])
                    self.assertEqual(code, 1)
                    self.assertIn('from ' + release.TARGETS[target][0], error.getvalue())
                    run.assert_not_called()

    def test_a_release_fetches_the_catalog_exports_the_kit_then_deploys(self):
        with tempfile.TemporaryDirectory() as directory:
            for target, dry in (('beta', True), ('production', True), ('beta', False)):
                out = Path(directory).resolve() / f'{target}-{dry}'
                with self.subTest(target=target, dry=dry), mock.patch.object(release, 'branch', return_value='staging'), \
                        mock.patch.object(release, 'published', return_value=True), \
                        mock.patch.object(release, 'outside_checkouts', return_value=True), \
                        mock.patch.object(release, 'run') as run, redirect_stdout(io.StringIO()):
                    flags = ['--dry-run'] if dry else []
                    self.assertEqual(release.main(['--stock-os', 'os.ldr', '--stock-content', 'content',
                                                   '--deploy', target, '--out', str(out), *flags]), 0)
                    commands = [[str(part) for part in call.args[0]] for call in run.call_args_list]
                    self.assertEqual(commands[0][1:], ['sdk.py', 'catalog', 'fetch', '--tags'])
                    self.assertEqual(commands[1][1:4], ['sdk.py', 'kit', '--collection'])
                    self.assertTrue(commands[1][4].endswith('collection.json'))
                    self.assertEqual(commands[2], ['npm', 'run', 'test:web'])
                    self.assertEqual(commands[3], ['node', 'tools/deploy_cloudflare.mjs',
                                                   *(['--env', 'beta'] if target == 'beta' else []), *flags])
                    self.assertEqual(run.call_args_list[3].kwargs['env']['OPEN_XY_KIT'], str(out / 'kit/kit.json'))

    def test_dry_runs_name_a_deployment_and_output_stays_outside_checkouts(self):
        with mock.patch.object(release, 'run') as run, redirect_stderr(io.StringIO()) as error:
            self.assertEqual(release.main(['--stock-os', 'os.ldr', '--stock-content', 'content', '--dry-run',
                                           '--out', 'unused']), 1)
            self.assertIn('--deploy', error.getvalue())
            self.assertEqual(release.main(['--stock-os', 'os.ldr', '--stock-content', 'content',
                                           '--out', str(release.ROOT / 'local/release-out')]), 1)
            self.assertIn('outside every checkout', error.getvalue())
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
