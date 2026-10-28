"""The synth voice adapter: every stock voice index gets an object, whatever the capacity."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling.adapters import generate_voice
from tooling.definitions import validate_definition
from test_sample_services import HOST_CXX
from machine_fixtures import fixture

ROOT = Path(__file__).resolve().parents[1]
# A machine whose voices remember their owner and render it, counting the
# lifecycle calls and any call for an owner the voice does not hold.
CORE = '''#pragma once
#include <opxy/audio.hpp>
namespace probe {
struct Voice { opxy::u32 owner, notes, initialized; };
struct Shared {};
}
'''
ENGINE = '''#include "core.h"
extern int initialized[257], destroyed[257], live, foreign;
namespace probe {
inline void initialize(Voice& voice, Shared&, opxy::u32 owner) {
    voice.owner = owner; ++voice.initialized; ++initialized[owner]; ++live;
}
inline void start(Voice& voice, Shared&, opxy::u32 owner) { foreign += voice.owner != owner; ++voice.notes; }
inline void destroy(Voice& voice, Shared&, opxy::u32 owner) { foreign += voice.owner != owner; ++destroyed[owner]; --live; }
inline void render(Voice& voice, Shared&, opxy::u32 owner, opxy::Output output, opxy::Parameters, opxy::Pitch) {
    foreign += voice.owner != owner;
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = static_cast<opxy::i32>(owner << 16);
}
}
'''


class SynthVoices(unittest.TestCase):
    def test_sample_engines_take_a_voice_for_every_stock_index(self):
        definition = copy.deepcopy(fixture('probesample').definition)
        validate_definition(definition, 'sample')
        for capacity in (1, 8, 255):
            definition['resources']['voiceCapacity'] = capacity
            with self.subTest(capacity=capacity), self.assertRaisesRegex(ValueError, '256'):
                validate_definition(definition, 'sample')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_every_voice_index_has_an_object_and_notes_share_the_capacity(self):
        definition = copy.deepcopy(fixture('probesynth').definition)
        definition['id'] = 'probe'
        for i in range(4):
            parameter = copy.deepcopy(definition['parameters'][i])
            parameter.update(id='extra' + str(i), label='Extra ' + str(i), control=i)
            definition['parameters'].append(parameter)
            for preset in definition['presets'].values():
                preset[parameter['id']] = parameter['default']
        definition['resources'].update(voiceBytes=12, voiceAlignment=4, voiceCapacity=2, sharedBytes=0)
        validate_definition(definition, 'synth')
        m = SimpleNamespace(definition=definition, prefix='probe', native_name='probe', identity=dict(engine=5, page=85))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            generate_voice(folder, m)
            (folder / 'core.h').write_text(CORE, encoding='utf-8')
            (folder / 'engine.cpp').write_text(ENGINE, encoding='utf-8')
            (folder / 'test.cpp').write_text((ROOT / 'tests/fixtures/synth_voice.cpp').read_text(encoding='utf-8'),
                                             encoding='utf-8')
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                       *('-I' + str(path) for path in (ROOT / 'sdk/include', ROOT / 'sdk/platform/opxy-1.2.1', folder)),
                       str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **hidden)
            self.assertEqual(result.returncode, 0, 'fixture step %d failed' % result.returncode)


if __name__ == '__main__':
    unittest.main()
