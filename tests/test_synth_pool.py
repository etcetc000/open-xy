"""The generic voice pool behaves exactly like the built-in voice adapter (voice.cpp.in)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT

FIXTURE = ROOT / 'tests/fixtures/synth_pool'


def adapter(capacity):
    """voice.cpp.in for the fake synth, expanded the way tooling/adapters.py does."""
    text = (ROOT / 'sdk/platform/opxy-1.2.1/voice.cpp.in').read_text(encoding='utf-8')
    values = dict(PREFIX='fake', NAMESPACE='fake', NAME='Fake', LABELS='"a"', CAPACITY=str(capacity),
                  VOICE_BYTES='32', VOICE_ALIGNMENT='4', SHARED_BYTES='0', SWEEP_DECLARATIONS='',
                  SWEEP_CONTROLS='        const opxy::i32* controls = parameters;\n')
    for key, value in values.items():
        text = text.replace('@' + key + '@', value)
    if '@' in text:
        raise ValueError('Unexpanded voice template')
    return text


class SynthPool(unittest.TestCase):
    def test_pool_matches_the_builtin_adapter_call_for_call(self):
        compiler = os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS') == '1' else []
        with tempfile.TemporaryDirectory() as temp:
            for capacity in (1, 3, 8, 40):
                voice = Path(temp) / f'voice-{capacity}.cpp'
                voice.write_text(adapter(capacity), encoding='utf-8')
                runner = Path(temp) / f'pool-{capacity}'
                built = subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', *flags,
                                        f'-DCAPACITY={capacity}', '-I' + str(FIXTURE), '-I' + str(ROOT / 'sdk/include'),
                                        '-I' + str(ROOT / 'sdk/platform'), str(FIXTURE / 'main.cpp'), str(voice),
                                        str(ROOT / 'sdk/platform/synth_pool.cpp'), '-o', str(runner)],
                                       capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr[:3000])
                for seed in range(1, 41):
                    result = subprocess.run([str(runner)], env={**os.environ, 'POOL_SEED': str(seed)},
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, f'capacity {capacity}, seed {seed}: {result.stderr}')


if __name__ == '__main__': unittest.main()
