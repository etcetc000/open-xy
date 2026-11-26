"""tools/idle_send_study.py, the host study docs/cpu-capabilities.md reports for the catalog's effects under idle
sends: it runs an effect folder through its grid and tells a send that idles and stays quiet from one that rises
again or never idles."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
FX = ROOT / 'tests/fixtures/machines/probefx'


def study(folder):
    with tempfile.TemporaryDirectory(prefix='idle-study-test-') as work:
        out = Path(work) / 'rows.json'
        run = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/idle_send_study.py'), '--cxx', HOST_CXX,
                              '--json', str(out), str(folder)], capture_output=True, text=True, timeout=600)
        if run.returncode:
            raise AssertionError(run.stdout[-2000:] + run.stderr[-2000:])
        return json.loads(out.read_text(encoding='utf-8')), run.stdout


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class IdleSendStudy(unittest.TestCase):
    def test_a_quiet_effect_idles_and_stays_below_the_floor(self):
        rows, table = study(FX)
        # Four controls: defaults, all 0, all 1, all 0.5 and each at 1: 8 settings, 3 inputs, 2 transports.
        self.assertEqual(len(rows), 8 * 3 * 2)
        self.assertTrue(all(r['idled'] and not r['woke'] for r in rows))
        self.assertIn('| probefx |', table)

    def test_an_effect_that_rises_again_or_never_idles_is_reported(self):
        with tempfile.TemporaryDirectory(prefix='idle-study-fx-') as work:
            folder = Path(work) / 'probefx'
            shutil.copytree(FX, folder)
            source = folder / 'fx.cpp'
            text = source.read_text(encoding='utf-8')
            # Its output comes back at 100 raw from block 6000 on (after any of its idles), and with Tone at 1
            # it rests at 100 raw from the start (so it never idles).
            old = '        right[i] = opxy::q31::multiply(clip(boost(right[i], drive)), state.level);\n    }\n'
            self.assertIn(old, text)
            text = text.replace(old, old + '    static unsigned blocks;\n'
                                '    if (++blocks >= 6000 || parameters[2] == 0x7fffffff)\n'
                                '        for (unsigned i = 0; i < opxy::block_frames; ++i) left[i] = 100;\n', 1)
            source.write_text(text, encoding='utf-8')
            rows, table = study(folder)
        never = [r for r in rows if not r['idled']]
        woke = [r for r in rows if r['woke']]
        self.assertEqual({r['setting'] for r in never}, {'tone at 1', 'all 1'})
        self.assertEqual({r['loudest_after'] for r in woke}, {100})
        self.assertEqual(len(woke) + len(never), len(rows))
        self.assertIn('rise again after idling', table)


if __name__ == '__main__':
    unittest.main()
