"""The site release: each deployment comes from its own branch."""
from contextlib import redirect_stderr
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


if __name__ == '__main__':
    unittest.main()
