"""The synth voice adapter: every stock voice index gets an object, whatever the capacity."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling import machines
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


class SynthVoices(unittest.TestCase):
    def test_sample_engines_take_a_voice_for_every_stock_voice_wrapper(self):
        # Stock passes its 27 voice wrappers' numbers, 0..26, as voice indices.
        definition = copy.deepcopy(fixture('probesample').definition)
        validate_definition(definition, 'sample')
        for capacity in (1, 8, 26):
            definition['resources']['voiceCapacity'] = capacity
            with self.subTest(capacity=capacity), self.assertRaisesRegex(ValueError, '27 or more'):
                validate_definition(definition, 'sample')
        for capacity in (27, 255):
            definition['resources']['voiceCapacity'] = capacity
            validate_definition(definition, 'sample')

    def run_fixture(self, capacity, source, engine=ENGINE, idle=False):
        definition = copy.deepcopy(fixture('probesynth').definition)
        definition['id'] = 'probe'
        for i in range(4):
            parameter = copy.deepcopy(definition['parameters'][i])
            parameter.update(id='extra' + str(i), label='Extra ' + str(i), control=i)
            definition['parameters'].append(parameter)
            for preset in definition['presets'].values():
                preset[parameter['id']] = parameter['default']
        definition['resources'].update(voiceBytes=12, voiceAlignment=4, voiceCapacity=capacity, sharedBytes=0)
        validate_definition(definition, 'synth')
        m = SimpleNamespace(kind='synth', definition=definition, prefix='probe', native_name='probe', identity=dict(engine=5, page=85))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            generate_voice(folder, m, idle=idle)
            (folder / 'core.h').write_text(CORE, encoding='utf-8')
            (folder / 'engine.cpp').write_text(engine, encoding='utf-8')
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
    def test_a_settled_synth_voice_tells_the_idle_cut_once_under_the_threshold(self):
        self.run_fixture(4, SETTLE_DRIVER, SETTLE_ENGINE, idle=True)

    def test_without_idle_a_settling_engines_adapter_carries_none_of_the_idle_cut(self):
        # Off unless asked: the build's idle capability, not the engine's `settled`, puts the
        # follower, its resets and the per-block peak scan in the adapter.
        m = SimpleNamespace(kind='synth', definition=fixture('probesynth').definition, prefix='probe',
                            native_name='probe', identity=dict(engine=5, page=85))
        with tempfile.TemporaryDirectory() as directory:
            off = generate_voice(Path(directory), m).read_text(encoding='utf-8')
            on = generate_voice(Path(directory), m, idle=True).read_text(encoding='utf-8')
        for name in ('idle_cut', 'settles', 'kSettles', 'sdk_idle', 'follow'):
            self.assertNotIn(name, off)
            self.assertIn(name, on)

    def test_settled_is_asked_only_of_machines_declaring_its_sdk(self):
        # settled() is SDK 1.22 API: an older machine's namespace may define one for its
        # own reasons, and the idle cut must not call it.
        definition = fixture('probesynth').definition
        for sdk, expected in (((1, 21), 'const bool kSettles = false;'),
                              (machines.IDLE_SDK, 'const bool kSettles = sdk_settles::has<Voice, Shared>::value;')):
            for kind in ('synth', 'drum'):
                d = copy.deepcopy(definition)
                if kind == 'drum':
                    d['resources'].update(modelCount=2)
                m = SimpleNamespace(kind=kind, definition=d, prefix='probe', native_name='probe',
                                    identity=dict(engine=5, page=85), sdk=sdk)
                with self.subTest(sdk=sdk, kind=kind), tempfile.TemporaryDirectory() as directory:
                    source = generate_voice(Path(directory), m, idle=True).read_text(encoding='utf-8')
                    self.assertIn(expected, source)
                    self.assertEqual('sdk_settles::query(' in source, sdk >= machines.IDLE_SDK)


# An engine that plays a level the driver sets and settles when the driver says.
SETTLE_ENGINE = ENGINE.replace('extern int destroyed[257];', 'extern int destroyed[257];\nextern opxy::i32 level;\nextern bool calm;', 1).replace(
    'output[i] = static_cast<opxy::i32>(voice.owner == owner ? owner << 16 : 0);',
    'output[i] = voice.owner == owner ? level : 0;', 1).replace(
    'inline void render(', 'inline bool settled(const Voice&, const Shared&) { return calm; }\ninline void render(', 1)
assert 'return calm;' in SETTLE_ENGINE and 'level : 0' in SETTLE_ENGINE
SETTLE_DRIVER = r"""#include "voice.cpp"
int destroyed[257];
opxy::i32 level = 1 << 28;
bool calm;
const void* registered_methods;
int (*ask)(void*);
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }
opxy::u32 sdk_voice_ticks() { return 0; }
extern "C" opxy::i32 sdk_idle_floor() { return 67908514; }  // -30 dB
extern "C" bool sdk_idle_register(const void* methods, int (*settled)(void*)) {
    registered_methods = methods;
    ask = settled;
    return true;
}
typedef int (*Start)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
const opxy::i32 parameters[8] = {};
int main() {
    void* h = probe_factory(3);
    const void* const* table = *static_cast<const void* const* const*>(h);
    reinterpret_cast<Start>(const_cast<void*>(table[4]))(h);
    opxy::i32 out[opxy::block_frames];
    if (ask) return 2;  // nothing registered before a render
    reinterpret_cast<Render>(const_cast<void*>(table[2]))(h, out, parameters);
    if (registered_methods != probe_vtable || !ask || ask(h)) return 3;
    // Quiet but not settled (a dip, a sustained note): never.
    level = 1000;
    for (int b = 0; b < 40; ++b) reinterpret_cast<Render>(const_cast<void*>(table[2]))(h, out, parameters);
    if (ask(h)) return 4;
    // Settled and quiet: after 8 blocks.
    calm = true;
    for (int b = 0; b < 7; ++b) reinterpret_cast<Render>(const_cast<void*>(table[2]))(h, out, parameters);
    if (ask(h)) return 5;
    reinterpret_cast<Render>(const_cast<void*>(table[2]))(h, out, parameters);
    if (!ask(h)) return 6;
    // A note again in place: its own peak, its own count.
    reinterpret_cast<Start>(const_cast<void*>(table[4]))(h);
    if (ask(h)) return 7;
    reinterpret_cast<Render>(const_cast<void*>(table[2]))(h, out, parameters);
    if (ask(h)) return 8;
    return 0;
}
"""

if __name__ == '__main__':
    unittest.main()
