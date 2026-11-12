"""The synth voice adapter: every stock voice index gets an object, whatever the capacity."""
import copy
import os
import struct
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
# A machine whose voices remember their owner and render it (scaled, so a fade shows).
CORE = '''#pragma once
#include <opxy/audio.hpp>
namespace probe {
struct Voice { opxy::u32 owner, notes, initialized; };
struct Shared {};
}
'''
ENGINE = '''#include "core.h"
extern int destroyed[257];
namespace probe {
inline void initialize(Voice& voice, Shared&, opxy::u32 owner) { voice.owner = owner; ++voice.initialized; }
inline void start(Voice& voice, Shared&, opxy::u32) { ++voice.notes; }
inline void destroy(Voice&, Shared&, opxy::u32 owner) { ++destroyed[owner]; }
inline void render(Voice& voice, Shared&, opxy::u32 owner, opxy::Output output, opxy::Parameters, opxy::Pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = static_cast<opxy::i32>(voice.owner == owner ? owner << 16 : 0);
}
}
'''

# The same, running stock drumline's voices (opxy/stock_drumline.hpp: silent on the host).
STOCK_CORE = '''#pragma once
#include <opxy/audio.hpp>
#include <opxy/stock_drumline.hpp>
namespace probe {
struct Voice { opxy::stock_drumline::Voice drum; };
struct Shared {};
}
'''
STOCK_ENGINE = '''#include "core.h"
extern int destroyed[257], released[257];
namespace probe {
inline opxy::stock_drumline::Patch patch() { return opxy::stock_drumline::Patch{0, 0, {0, 0, 0, 0}, false, 1 << 30}; }
inline void initialize(Voice& voice, Shared&, opxy::u32) { opxy::stock_drumline::construct(voice.drum); }
inline void start(Voice& voice, Shared&, opxy::u32) { opxy::stock_drumline::start(voice.drum, patch()); }
inline void destroy(Voice& voice, Shared&, opxy::u32 owner) { opxy::stock_drumline::end(voice.drum); ++destroyed[owner]; }
inline void release(Voice& voice, Shared&, opxy::u32 owner) { opxy::stock_drumline::note_off(voice.drum); ++released[owner]; }
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch) {
    opxy::stock_drumline::render(voice.drum, patch(), output, parameters, pitch);
}
inline const char* parameter_title(opxy::u32 index) { return index == 1 ? "titled" : nullptr; }
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

    def run_fixture(self, capacity, source, stock_voice=False):
        definition = copy.deepcopy(fixture('probesynth').definition)
        definition['id'] = 'probe'
        for i in range(4):
            parameter = copy.deepcopy(definition['parameters'][i])
            parameter.update(id='extra' + str(i), label='Extra ' + str(i), control=i)
            definition['parameters'].append(parameter)
            for preset in definition['presets'].values():
                preset[parameter['id']] = parameter['default']
        definition['resources'].update(voiceBytes=12, voiceAlignment=4, voiceCapacity=capacity, sharedBytes=0)
        if stock_voice:
            # opxy::stock_drumline::Voice holds two pointers: 896 bytes on the device's 32-bit core,
            # 912 with a 64-bit host's pointers and their padding.
            pointer = struct.calcsize('P')
            definition['resources'].update(stockVoice='drumline', releaseCallback=1, voiceBytes=896 if pointer == 4 else 912,
                                           voiceAlignment=pointer)
        validate_definition(definition, 'synth')
        m = SimpleNamespace(kind='synth', definition=definition, prefix='probe', native_name='probe', identity=dict(engine=5, page=85))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            generate_voice(folder, m)
            (folder / 'core.h').write_text(STOCK_CORE if stock_voice else CORE, encoding='utf-8')
            (folder / 'engine.cpp').write_text(STOCK_ENGINE if stock_voice else ENGINE, encoding='utf-8')
            (folder / 'test.cpp').write_text(source, encoding='utf-8')
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                       *('-I' + str(path) for path in (ROOT / 'sdk/include', ROOT / 'sdk/platform/opxy-1.2.1', folder)),
                       str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **hidden)
            self.assertEqual(result.returncode, 0, 'fixture step %d failed' % result.returncode)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_every_voice_index_has_an_object_and_notes_share_the_capacity(self):
        self.run_fixture(2, (ROOT / 'tests/fixtures/synth_voice.cpp').read_text(encoding='utf-8'))

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_pool_never_exceeds_stock_voice_slots_and_a_spare(self):
        # Stock sounds at most 27 notes; a capacity of 256 keeps 28 voices.
        self.run_fixture(256, '''#include "voice.cpp"
int destroyed[257];
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }
opxy::u32 sdk_voice_ticks() { return 0; }
int main() { return sizeof(voices) / sizeof(voices[0]) == 28 ? 0 : 1; }
''')
        # A capacity of 1 keeps its voice and the spare.
        self.run_fixture(1, '''#include "voice.cpp"
int destroyed[257];
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }
opxy::u32 sdk_voice_ticks() { return 0; }
int main() { return sizeof(voices) / sizeof(voices[0]) == 2 ? 0 : 1; }
''')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_a_stock_voice_synth_hears_its_key_go_up(self):
        # Method +28, the release callback (SDK 1.12), ends the stock voice's note; +40 is
        # drumline's level; render first tells the binding which handle renders; a
        # parameter is named by parameter_title where it gives a name.
        self.run_fixture(2, '''#include "voice.cpp"
int destroyed[257], released[257];
const void* rendering;
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index; }
opxy::u32 sdk_voice_ticks() { return 0; }
void sdk_drumline_caller(const void* handle, const void*) { rendering = handle; }
opxy::i32 sdk_drumline_level(const void*) { return 7; }
typedef int (*Method)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
int main() {
    void* handle = probe_factory(4);
    const void* const* table = *static_cast<const void* const* const*>(handle);
    reinterpret_cast<Method>(const_cast<void*>(table[4]))(handle);
    opxy::i32 output[64], parameters[16] = {};
    reinterpret_cast<Render>(const_cast<void*>(table[2]))(handle, output, parameters);
    if (rendering != handle || output[0] || output[63]) return 1;
    reinterpret_cast<Method>(const_cast<void*>(table[7]))(handle);
    if (released[5] != 1 || destroyed[5]) return 2;
    if (table[10] != reinterpret_cast<const void*>(&sdk_drumline_level)) return 3;
    const char* titled = probe_parameter_name(1);
    const char* label = probe_parameter_name(0);
    if (titled[0] != 't' || label[0] == 't') return 4;
    return 0;
}
''', stock_voice=True)

    def test_other_synths_keep_stocks_level(self):
        m = SimpleNamespace(kind='synth', definition=fixture('probesynth').definition, prefix='probe', native_name='probe')
        with tempfile.TemporaryDirectory() as directory:
            source = generate_voice(directory, m).read_text(encoding='utf-8')
        self.assertNotIn('note_off', source)
        self.assertNotIn('drumline', source)
        self.assertIn('reinterpret_cast<const void*>(&zero), reinterpret_cast<const void*>(&noop),', source)
        self.assertIn('reinterpret_cast<const void*>(opxy::platform::kVoiceDefaultMethod)', source)


if __name__ == '__main__':
    unittest.main()
