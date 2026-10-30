"""Exercise the generated drum adapter's parameter transport and voice lifecycle."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling.adapters import generate_voice
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
CORE = '''#pragma once
#include <opxy/audio.hpp>
extern opxy::i32 captured[8];
extern opxy::u32 fixture_pitch[opxy::block_frames], captured_pitch[opxy::block_frames];
extern opxy::i32 fixture_amplitude[opxy::block_frames];
extern bool negative_output;
extern unsigned destroyed[257], initialized[257], started[257];
namespace probe {
struct Voice { opxy::u32 owner; };
struct Shared {};
}
'''
ENGINE = '''#include "core.h"
namespace probe {
inline void initialize(Voice& v, Shared&, opxy::u32 id) { v.owner = id; ++initialized[id]; }
inline void start(Voice&, Shared&, opxy::u32 id) { ++started[id]; }
inline void destroy(Voice&, Shared&, opxy::u32 id) { ++destroyed[id]; }
inline void drum_defaults(opxy::u32 key, opxy::u8* raw) {
    raw[0] = key % 30;
    for (unsigned i = 1; i < 8; ++i) raw[i] = key;
}
inline const char* parameter_name(opxy::u32, opxy::u32) { return "parameter"; }
inline const char* model_name(opxy::u32) { return "model"; }
inline opxy::u32 parameter_count(opxy::u32) { return 8; }
inline void render(Voice& v, Shared&, opxy::u32 id, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    for (unsigned i = 0; i < 8; ++i) captured[i] = parameters[i];
    for (unsigned i = 0; i < opxy::block_frames; ++i) captured_pitch[i] = pitch[i];
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        output[i] = v.owner == id ? (negative_output ? -opxy::i32(id << 16) : opxy::i32(id << 16)) : -1;
}
}
'''
DRIVER = r'''#include "voice.cpp"
#include <cstdio>
#include <cstdint>
const opxy::i32* fixture_region;
opxy::i32 captured[8];
opxy::u32 fixture_pitch[opxy::block_frames], captured_pitch[opxy::block_frames];
opxy::i32 fixture_amplitude[opxy::block_frames];
bool negative_output;
unsigned destroyed[257], initialized[257], started[257];
volatile opxy::i32 sdk_sample_drum_octaves[8];
extern "C" const char probe_engine_id[] = "probe";
const char* sdk_drum_title(opxy::u32, opxy::u32) { return "parameter"; }
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
namespace {
void* objects[256];
opxy::i32 offsets[8];
typedef int (*Start)(void*, opxy::u32, opxy::u32, const opxy::i32*, const opxy::i32*);
typedef int (*End)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*, const opxy::i32*);
const void* const* table(unsigned id) { return *static_cast<const void* const* const*>(objects[id]); }
void note(unsigned id, unsigned key) {
    reinterpret_cast<Start>(const_cast<void*>(table(id)[5]))(objects[id], key, 100, fixture_region, offsets);
}
void end(unsigned id) { reinterpret_cast<End>(const_cast<void*>(table(id)[0]))(objects[id]); }
bool is_active(unsigned id) {
    return reinterpret_cast<End>(const_cast<void*>(table(id)[6]))(objects[id]) != 0;
}
opxy::i32 rendered(unsigned id, const opxy::i32* params = offsets) {
    opxy::i32 output[opxy::block_frames];
    for (auto& sample : output) sample = -9;
    reinterpret_cast<Render>(const_cast<void*>(table(id)[3]))(objects[id], output, fixture_region, params);
    for (auto sample : output) if (sample != output[0]) return -2;
    return output[0] ? (output[0] + 1) / 65536 : 0;
}
unsigned model() { return static_cast<unsigned>((std::uint64_t(captured[0]) * 30) >> 31); }
unsigned raw(unsigned slot) { return static_cast<unsigned>(captured[slot]) >> 24; }
}
int main() {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        fixture_pitch[i] = i * 1040;
        fixture_amplitude[i] = 0x7fffffff;
    }
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    for (unsigned i = 0; i < 256; ++i) CHECK(objects[i] = probe_factory(i, i % 8));
    CHECK(!probe_factory(256, 0));
    CHECK(rendered(0) == 0);
    CHECK(!is_active(0));
    // Track one's octave turns raw note 53 into pad 65; defaults must use 65.
    sdk_sample_drum_octaves[1] = 1;
    note(1, 53);
    CHECK(rendered(1) == 2 && model() == 5 && raw(7) == 65);
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        CHECK(captured_pitch[i] == fixture_pitch[i] * 65536u);
    CHECK(initialized[2] == 1 && started[2] == 1);
    // Stock mixes the current block, then retires a voice whose +24 query is
    // zero. Exercise that sequence so a one-block click cannot pass lifecycle.
    unsigned blocks = 0;
    for (; blocks < 64; ++blocks) {
        CHECK(rendered(1) == 2);
        if (!is_active(1)) { end(1); break; }
    }
    CHECK(blocks == 64 && destroyed[2] == 0);
    // The alternate path supplies a per-sample envelope; apply it exactly once
    // to both signs, including zero, quarter/half scale and maximum gain.
    const opxy::i32 gains[] = {0, 0x20000000, 0x40000000, 0x7fffffff};
    const opxy::i32 positive[] = {0, 32768, 65536, 131071};
    const opxy::i32 negative[] = {0, -32768, -65536, -131072};
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = gains[i % 4];
    for (unsigned sign = 0; sign < 2; ++sign) {
        negative_output = sign != 0;
        opxy::i32 output[opxy::block_frames];
        reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], output, fixture_region, offsets);
        for (unsigned i = 0; i < opxy::block_frames; ++i)
            CHECK(output[i] == (sign ? negative[i % 4] : positive[i % 4]));
    }
    negative_output = false;
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = 0x7fffffff;
    note(0, 53);
    CHECK(rendered(0) == 1 && model() == 23 && raw(7) == 53);
    // A call through the other renderer's +8 entry must overwrite stale scratch.
    opxy::i32 silent[opxy::block_frames];
    for (auto& sample : silent) sample = -9;
    typedef int (*NormalRender)(void*, opxy::i32*, const opxy::i32*);
    reinterpret_cast<NormalRender>(const_cast<void*>(table(0)[2]))(objects[0], silent, offsets);
    for (auto sample : silent) CHECK(sample == 0);
    opxy::i32 payload[8] = {};
    opxy::u8 controls[8];
    fixture_region = payload;
    // Exercise each 7-bit value in every non-model slot and every model bin.
    for (unsigned value = 0; value < 128; ++value) {
        controls[0] = value % 30;
        for (unsigned i = 1; i < 8; ++i) controls[i] = (value + i * 17) % 128;
        CHECK(opxy::platform::drum_pack(controls, payload + 1));
        CHECK(rendered(1) == 2 && model() == controls[0]);
        for (unsigned i = 1; i < 8; ++i) {
            CHECK(raw(i) == controls[i]);
            CHECK(captured[i] == (controls[i] == 127 ? 0x7fffffff : opxy::i32(controls[i]) << 24));
        }
        CHECK(rendered(1, nullptr) == 2 && model() == controls[0]);
        for (unsigned i = 1; i < 8; ++i) CHECK(raw(i) == controls[i]);
    }
    controls[0] = 10;
    for (unsigned i = 1; i < 8; ++i) controls[i] = 64;
    CHECK(opxy::platform::drum_pack(controls, payload + 1));
    // Offsets act on decoded controls: model remains in the requested enum bin.
    offsets[0] = 67 << 24; offsets[1] = 0; offsets[2] = 0x7fffffff;
    CHECK(rendered(1) == 2 && model() == 13 && raw(1) == 0 && raw(2) == 127);
    offsets[0] = 0x7fffffff;
    CHECK(rendered(1) == 2 && model() == 29);
    offsets[0] = 0;
    CHECK(rendered(1) == 2 && model() == 0);
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    // Corrupt format tag and a valid payload with an unavailable model both use pad defaults.
    payload[4] ^= 1 << 27;
    CHECK(rendered(1) == 2 && model() == 5 && raw(7) == 65);
    controls[0] = 127;
    CHECK(opxy::platform::drum_pack(controls, payload + 1));
    CHECK(rendered(1) == 2 && model() == 5 && raw(7) == 65);
    fixture_region = nullptr;
    // Capacity two: the oldest note (handle one) is destroyed and goes silent.
    note(200, 53);
    CHECK(destroyed[2] == 1 && rendered(1) == 0 && rendered(200) == 201 && rendered(0) == 1);
    CHECK(!is_active(1) && is_active(200) && is_active(0));
    note(0, 54);
    CHECK(initialized[1] == 1 && started[1] == 2 && rendered(0) == 1 && raw(7) == 54);
    end(200);
    CHECK(destroyed[201] == 1 && rendered(200) == 0);
    CHECK(!is_active(200));
    note(255, 127);
    CHECK(rendered(255) == 256 && destroyed[1] == 0);
    end(1);
    CHECK(destroyed[2] == 1);
    for (unsigned n = 0; n < 2000; ++n) {
        unsigned id = n * 37 % 256;
        note(id, n % 128);
        CHECK(rendered(id) == opxy::i32(id + 1));
    }
    return 0;
}
'''


class DrumVoices(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_native_parameter_transport_and_voice_lifecycle(self):
        definition = copy.deepcopy(fixture('probesynth').definition)
        definition['id'] = 'probe'
        definition['resources'].update(voiceBytes=4, voiceAlignment=4, voiceCapacity=2,
                                       sharedBytes=0, modelCount=30)
        machine = SimpleNamespace(kind='drum', definition=definition, prefix='probe', native_name='probe')
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = generate_voice(folder, machine)
            # The actual +12 ABI transports its payload directly; no global
            # payload-pointer substitution is needed to execute the adapter.
            generated = source.read_text(encoding='utf-8')
            self.assertNotIn('0x0803b9c4', generated)
            native_pitch = 'reinterpret_cast<const opxy::u32*>(opxy::platform::kPitchIncrements)'
            self.assertEqual(generated.count(native_pitch), 1)
            native_amplitude = 'reinterpret_cast<const opxy::i32*>(0x11903400)'
            self.assertEqual(generated.count(native_amplitude), 1)
            source.write_text(generated.replace(native_pitch, 'fixture_pitch').replace(
                native_amplitude, 'fixture_amplitude'), encoding='utf-8')
            for name, content in (('core.h', CORE), ('engine.cpp', ENGINE), ('test.cpp', DRIVER)):
                (folder / name).write_text(content, encoding='utf-8')
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                       *('-I' + str(path) for path in (ROOT / 'sdk/include', ROOT / 'sdk/platform/opxy-1.2.1', folder)),
                       str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
