"""Opt-in synth stereo contract: real adapters and asymmetric L/R audio."""
import copy
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling.adapters import generate_voice, generate_voice_preview
from tooling.definitions import validate_definition
from tooling.preview import VOICE_EXPORTS, FLAGS, MEMORY
from tooling import cpu
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
WASM_CXX = os.environ.get('OPXY_TEST_PREVIEW_CXX', 'clang++')
CORE = '''#pragma once
#include <opxy/audio.hpp>
namespace probe { struct Voice { opxy::u32 count; }; struct Shared {}; }
'''
ENGINE = '''#include "core.h"
namespace probe {
inline void initialize(Voice& v, Shared&, opxy::u32) { v.count = 0; }
inline void start(Voice& v, Shared&, opxy::u32) { v.count = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}
inline void render(Voice& v, Shared&, opxy::u32, opxy::Output out, opxy::Parameters, opxy::Pitch) {
    for (unsigned i = 0; i < 64; ++i) {
        out[2*i] = 1000 + v.count;
        out[2*i+1] = -3000 - 2 * v.count;
        ++v.count;
    }
}
}
'''
NATIVE = r'''#include "voice.cpp"
opxy::u32 sdk_voice_ticks() { return 0; }
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 i) { return i; }
int main() {
    opxy::i32 guarded[130], parameters[8] = {};
    void* h = probe_factory(0);
    const void* const* table = *static_cast<const void* const* const*>(h);
    if (reinterpret_cast<int(*)()>(const_cast<void*>(table[8]))() != 1) return 1;
    auto render = [&](void* handle) {
        for (auto& x : guarded) x = 1234567;
        probe_render(handle, guarded + 1, parameters);
        return guarded[0] == 1234567 && guarded[129] == 1234567;
    };
    if (!render(h)) return 2;
    for (unsigned i = 1; i <= 128; ++i) if (guarded[i]) return 3;
    probe_start(h);
    if (!render(h)) return 4;
    for (unsigned i = 0; i < 64; ++i)
        if (guarded[1+2*i] != 1000+int(i) || guarded[2+2*i] != -3000-2*int(i)) return 5;
    // Fill the spare, then take a sounding voice. Both channels fade together.
    void* other = probe_factory(1); probe_start(other); render(other);
    void* next = probe_factory(2); probe_start(next);
    Slot* taken = pending(static_cast<Handle*>(next)); if (!taken) return 6;
    void* previous = &handles[taken->owner - 1];
    if (!render(previous) || guarded[1] <= 0 || guarded[2] >= 0 || guarded[127] || guarded[128]) return 7;
    if (!render(previous)) return 8;
    for (unsigned i = 1; i <= 128; ++i) if (guarded[i]) return 9;
    if (!render(next) || guarded[1] != 1000 || guarded[2] != -3000) return 10;
    return 0;
}
'''


def definition():
    value = copy.deepcopy(fixture('probesynth').definition)
    value['id'] = 'probe'
    value['resources'].update(voiceBytes=4, voiceAlignment=4, voiceCapacity=1,
                              sharedBytes=0, stereoOutput=1)
    return value


class StereoSynth(unittest.TestCase):
    def run_command(self, command):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=120, **HIDDEN)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_schema_opt_in_and_rejection(self):
        value = definition()
        for flag in (0, 1):
            value['resources']['stereoOutput'] = flag
            validate_definition(value, 'synth')
        for flag in (-1, 2, True, '1'):
            value['resources']['stereoOutput'] = flag
            with self.assertRaises(ValueError):
                validate_definition(value, 'synth')
        del value['resources']['stereoOutput']
        validate_definition(value, 'synth')

    def test_omitted_stereo_flag_matches_explicit_mono(self):
        # Mono uses the current upstream allocator, exercised by test_voices.
        # An explicit zero must not select the separate Field voice adapter.
        value = definition()
        del value['resources']['stereoOutput']
        m = SimpleNamespace(definition=value, prefix='probe', native_name='probe')
        with tempfile.TemporaryDirectory() as folder:
            source = generate_voice(folder, m).read_text()
            self.assertIn('i < opxy::block_frames;', source)
            self.assertNotIn('static int stereo()', source)
            value['resources']['stereoOutput'] = 0
            self.assertEqual(source, generate_voice(folder, m).read_text())

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_native_vtable_and_silence_canaries(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            m = SimpleNamespace(definition=definition(), prefix='probe', native_name='probe')
            generate_voice(folder, m)
            (folder / 'core.h').write_text(CORE)
            (folder / 'engine.cpp').write_text(ENGINE)
            (folder / 'test.cpp').write_text(NATIVE)
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            self.run_command([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                              '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                              folder / 'test.cpp', '-o', binary])
            self.run_command([binary])
            m.id, m.kind, m.root, m.sources, m.includes = 'probe', 'synth', folder, (), ()
            self.assertGreater(cpu.block_cost(m, HOST_CXX, folder), 0)
            self.assertIn('output[opxy::block_frames * 2]', (folder / 'cpu-probe/bench.cpp').read_text())

    @unittest.skipUnless(shutil.which(WASM_CXX) and shutil.which('node'), 'WASM compiler and Node required')
    def test_wasm_browser_channels_and_canaries(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / 'core.h').write_text(CORE)
            (folder / 'engine.cpp').write_text(ENGINE)
            source = generate_voice_preview(folder, definition())
            binary = folder / 'engine.wasm'
            self.run_command([WASM_CXX, *FLAGS, '-I' + str(ROOT / 'sdk/include'), source,
                              ROOT / 'sdk/platform/blackfin/memory.cpp', '-o', binary, '-Wl,--no-entry',
                              '-Wl,-z,stack-size=16384', f'-Wl,--initial-memory={MEMORY}',
                              f'-Wl,--max-memory={MEMORY}', *('-Wl,--export=' + x for x in VOICE_EXPORTS)])
            self.run_command(['node', ROOT / 'tools/check_synth_stereo.mjs', binary])


if __name__ == '__main__':
    unittest.main()
