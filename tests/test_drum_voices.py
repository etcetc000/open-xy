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
extern opxy::u32 captured_model;
extern opxy::u32 fixture_pitch[opxy::block_frames], captured_pitch[opxy::block_frames];
extern opxy::i32 fixture_amplitude[opxy::block_frames];
extern bool negative_output;
extern unsigned destroyed[257], initialized[257], started[257];
extern unsigned bench_rendered[12][128], clock_value;
// Stock's track filter (drum_filter.hpp), as host doubles: M3's on/off and type
// per track, the voice's filter envelope, and a factory building fake objects.
opxy::u8 fixture_filter_on[16];
opxy::u32 fixture_filter_type[16];
opxy::i32 fixture_filter_envelope[64];
// The filter doubles: a fake object per pool slot, its method table at +0x8
// (process) and +0x14 (reset) as stock's (drum_filter.hpp).
struct FakeFilter {
    const void* const* vtable;
    opxy::u32 type, index, track, resets, runs;
    opxy::i32 key, envelope, first;
    const opxy::i32* record;
};
FakeFilter fake_pool[128];
unsigned fake_builds, fake_runs;
// svf (10) doubles the block; the ladder (16) leaves it; z hipass (17) drives it past full scale both ways.
void fake_process(void* self, opxy::i32* block, const opxy::i32* record, opxy::i32 key, opxy::i32 envelope) {
    FakeFilter* f = static_cast<FakeFilter*>(self);
    ++f->runs; ++fake_runs;
    f->record = record; f->key = key; f->envelope = envelope; f->first = block[0];
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        block[i] = f->type == 10 ? block[i] * 2 : f->type == 17 ? ((i & 1) ? 0x40000000 : -0x40000000) : block[i];
}
void fake_reset(void* self, opxy::u32 value) { if (!value) ++static_cast<FakeFilter*>(self)->resets; }
const void* const fake_methods[11] = {nullptr, nullptr, reinterpret_cast<const void*>(&fake_process), nullptr, nullptr,
                                      reinterpret_cast<const void*>(&fake_reset)};
// Stock's factory constructs in slot `index` (below 128); type 18 has no filter.
opxy::u32 fixture_filter_factory(opxy::u32 type, void** object, opxy::u32 index, opxy::u32 track) {
    if (index >= 128 || type >= 19 || type == 18) { *object = nullptr; return 0; }
    FakeFilter& f = fake_pool[index];
    f.vtable = fake_methods; f.type = type; f.index = index; f.track = track; f.runs = 0;
    *object = &f;
    ++fake_builds;
    return 1;
}
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
inline void drum_defaults(opxy::u32 key, opxy::u8* model, opxy::u8* raw) {
    *model = key % 12;
    for (unsigned i = 0; i < 8; ++i) raw[i] = key;
}
inline void drum_select(Voice&, Shared&, opxy::u32, opxy::u32 model) { captured_model = model; }
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
#include <cstring>
#include <cstdint>
const opxy::i32* fixture_region;
opxy::i32 captured[8];
opxy::u32 captured_model = 99;
opxy::u32 fixture_pitch[opxy::block_frames], captured_pitch[opxy::block_frames];
opxy::i32 fixture_amplitude[opxy::block_frames];
bool negative_output;
unsigned destroyed[257], initialized[257], started[257];
unsigned bench_rendered[12][128], clock_value;
volatile opxy::i32 sdk_track_octaves[8];
extern "C" const char probe_engine_id[] = "probe";
const char* sdk_drum_title(opxy::u32, opxy::u32) { return "parameter"; }
extern "C" opxy::u32 sdk_benchmark_cycles() { return 0; }
extern "C" bool probe_drum_pad_text(opxy::u32, opxy::i32, char*);
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
unsigned model() { return captured_model; }
// Stock's payload word for a model byte of Drumline type 7: the kit's mark.
const opxy::i32 kMarked = opxy::i32(42u * 32537631u + 16268815u);
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
    // Stock asks for an index's object again before each note: the note that ended
    // there frees its voice, so no voice is lost to a retired note.
    note(9, 53);
    CHECK(initialized[10] == 1);
    const unsigned freed = destroyed[10];
    CHECK(objects[9] = probe_factory(9, 1));
    CHECK(destroyed[10] == freed + 1 && rendered(9) == 0);
    // Track one's octave turns raw note 53 into pad 65; defaults must use 65.
    sdk_track_octaves[1] = 1;
    note(1, 53);
    CHECK(rendered(1) == 2 && model() == 5 && raw(7) == 65 && raw(0) == 65);
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
    CHECK(rendered(0) == 1 && model() == 5 && raw(7) == 53);
    // A call through the other renderer's +8 entry must overwrite stale scratch.
    opxy::i32 silent[opxy::block_frames];
    for (auto& sample : silent) sample = -9;
    typedef int (*NormalRender)(void*, opxy::i32*, const opxy::i32*);
    reinterpret_cast<NormalRender>(const_cast<void*>(table(0)[2]))(objects[0], silent, offsets);
    for (auto sample : silent) CHECK(sample == 0);
    opxy::i32 payload[8] = {};
    opxy::u8 controls[8];
    fixture_region = payload;
    payload[0] = kMarked;
    // Exercise each 7-bit value in every slot and every model.
    for (unsigned value = 0; value < 128; ++value) {
        const unsigned m = value % 12;
        for (unsigned i = 0; i < 8; ++i) controls[i] = (value + i * 17) % 128;
        CHECK(opxy::platform::drum_pack(m, controls, payload + 1));
        CHECK(rendered(1) == 2 && model() == m);
        for (unsigned i = 0; i < 8; ++i) {
            CHECK(raw(i) == controls[i]);
            CHECK(captured[i] == (controls[i] == 127 ? 0x7fffffff : opxy::i32(controls[i]) << 24));
        }
        CHECK(rendered(1, nullptr) == 2 && model() == m);
        for (unsigned i = 0; i < 8; ++i) CHECK(raw(i) == controls[i]);
    }
    // Without padMix the pad's level (word 7) never reaches the voice, nor
    // without padControls its pad word (word 6), even on a key set up.
    payload[7] = 1 << 26;
    payload[6] = static_cast<opxy::i32>(0x7a5a0000u | opxy::platform::kDrumPadReady);
    CHECK(rendered(1) == 2);
    payload[7] = payload[6] = 0;
    char caption[16] = {};
    CHECK(!probe_drum_pad_text(0, 3, caption));
    for (unsigned i = 0; i < 8; ++i) controls[i] = 64;
    CHECK(opxy::platform::drum_pack(10, controls, payload + 1));
    // The eight offsets act on the eight decoded controls; the model has none.
    offsets[0] = 67 << 24; offsets[1] = 0; offsets[2] = 0x7fffffff; offsets[7] = 0x40000000 - (5 << 24);
    CHECK(rendered(1) == 2 && model() == 10 && raw(0) == 67 && raw(1) == 0 && raw(2) == 127 && raw(7) == 59);
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    // An unmarked pad (stock's defaults, or the earlier eight-value layout)
    // and a marked pad with a model the kit lacks both use the pad defaults.
    payload[0] = opxy::i32(2u * 32537631u + 16268815u);
    CHECK(rendered(1) == 2 && model() == 5 && raw(7) == 65);
    payload[0] = kMarked;
    CHECK(opxy::platform::drum_pack(13, controls, payload + 1));
    CHECK(rendered(1) == 2 && model() == 5 && raw(7) == 65);
    payload[4] |= 1;   // words stock never writes
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


# A kit that opts into chokeSameKey and defines the finished hook.
CHOKE_ENGINE = ENGINE.replace('#include "core.h"', '#include "core.h"\nextern bool done[257];', 1).replace(
    'inline const char* parameter_name', """inline bool finished(const Voice& v, const Shared&) { return done[v.owner]; }
inline const char* parameter_name""", 1)
CHOKE_DRIVER = DRIVER[:DRIVER.index('int main() {')].replace(
    'unsigned destroyed[257]', 'bool done[257];\nunsigned destroyed[257]') + r"""
// Every frame of the handle's next block, in units of 1/65536 of full scale.
bool block(unsigned id, opxy::i32* output) {
    reinterpret_cast<Render>(const_cast<void*>(table(id)[3]))(objects[id], output, nullptr, offsets);
    return true;
}
int main() {
    (void)kMarked;
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = 0x7fffffff;
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    for (unsigned i = 0; i < 256; ++i) CHECK(objects[i] = probe_factory(i, i % 8));
    // Four voices. Handles 1, 9, 17 and 25 play track one; 2 plays track two.
    note(1, 53);
    CHECK(rendered(1) == 2 && is_active(1));
    // Another track's same key, and the same track's other keys, never choke.
    note(2, 53);
    note(17, 54);
    CHECK(rendered(1) == 2 && rendered(2) == 3 && rendered(17) == 18);
    // The same pad an octave pair up (77 plays pad 53) cuts handle one's voice:
    // it fades out linearly over 128 frames, two blocks, inside its render.
    note(9, 77);
    CHECK(rendered(9) == 10 && is_active(1) && destroyed[2] == 0);
    opxy::i32 output[opxy::block_frames];
    const opxy::i32 full = opxy::q31::multiply(2 << 16, 0x7fffffff);   // after stock's gain
    for (unsigned b = 0; b < 2; ++b) {
        CHECK(block(1, output));
        for (unsigned i = 0; i < opxy::block_frames; ++i)
            CHECK(output[i] == opxy::q31::multiply(full, opxy::i32(127 - (b * 64 + i)) << 24));
        CHECK(output[0] > output[opxy::block_frames - 1] && (b == 0 || output[opxy::block_frames - 1] == 0));
        CHECK(b == 1 || (is_active(1) && destroyed[2] == 0));
    }
    // Then it is freed: +24 reports it inactive and it renders silence.
    CHECK(destroyed[2] == 1 && !is_active(1) && rendered(1) == 0);
    end(1);
    CHECK(destroyed[2] == 1);
    CHECK(rendered(9) == 10 && rendered(2) == 3 && rendered(17) == 18);
    // A retriggered handle keeps its own voice without fading it.
    note(9, 53);
    CHECK(rendered(9) == 10 && rendered(9) == 10 && is_active(9) && started[10] == 2);
    // With every voice busy, one already fading goes to a new note before the
    // oldest: handle two's note (the oldest) keeps sounding.
    note(25, 54);     // track one: cuts handle 17
    CHECK(rendered(25) == 26 && is_active(17));
    note(3, 60);
    CHECK(destroyed[18] == 1 && !is_active(17) && rendered(17) == 0);
    CHECK(rendered(2) == 3 && rendered(3) == 4 && rendered(25) == 26 && rendered(9) == 10);
    // The finished hook: +24 reports the voice inactive and frees it at once.
    done[4] = true;
    CHECK(!is_active(3) && destroyed[4] == 1 && rendered(3) == 0);
    end(3);
    CHECK(destroyed[4] == 1);
    note(4, 60);      // takes the voice handle three freed, not a sounding one
    CHECK(initialized[5] == 1 && rendered(4) == 5);
    CHECK(rendered(2) == 3 && rendered(25) == 26 && rendered(9) == 10 && is_active(2));
    return 0;
}
"""


# A padMix kit: the payload's word 7, Q27 with 0 dB unity, cuts each voice of a
# key the kit set up; a boost, a missing word, no payload and a key the kit has
# not set up (an SDK 1.9 project's, whatever stock left there) play at unity.
MIX_DRIVER = DRIVER[:DRIVER.index('int main() {')] + r"""
opxy::i32 frame(unsigned id, const opxy::i32* region) {
    opxy::i32 output[opxy::block_frames];
    reinterpret_cast<Render>(const_cast<void*>(table(id)[3]))(objects[id], output, region, offsets);
    for (auto sample : output) if (sample != output[0]) return -2;
    return output[0];
}
int main() {
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = 0x7fffffff;
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    for (unsigned i = 0; i < 256; ++i) CHECK(objects[i] = probe_factory(i, i % 8));
    // Each note plays the level of its first block's payload (a new note per value).
    note(1, 53);
    const opxy::i32 unity = opxy::q31::multiply(2 << 16, 0x7fffffff);
    CHECK(frame(1, nullptr) == unity);
    // Keys an SDK 1.9 kit stored, or never stored: stock's default kit gives -2 dB
    // (8403a79c) and, on its last key, 0x26660000 in the pad word; its editor a
    // Q31 with a low half. None plays its level.
    const opxy::i32 old_words[] = {0, 0x26660000, opxy::i32(199 * 10737418 + 5368709), opxy::i32(5368709),
                                   opxy::i32(0x80000000u | opxy::platform::kDrumPadReady)};
    for (opxy::i32 old_word : old_words) {
        opxy::i32 old[8] = {kMarked, 0, 0, 0, 0, 0, old_word, 1 << 26};
        note(1, 53);
        CHECK(frame(1, old) == unity);
        old[0] = 0;
        note(1, 53);
        CHECK(frame(1, old) == unity);
    }
    opxy::i32 unmarked[8] = {0, 0, 0, 0, 0, 0, opxy::i32(opxy::platform::kDrumPadReady), 1 << 26};
    note(1, 53);
    CHECK(frame(1, unmarked) == unity);
    opxy::i32 payload[8] = {kMarked, 0, 0, 0, 0, 0, opxy::i32(opxy::platform::kDrumPadReady), 0};
    note(1, 53);
    CHECK(frame(1, payload) == unity);                      // no level word
    payload[7] = 1 << 27;                                   // 0 dB
    note(1, 53);
    CHECK(frame(1, payload) == unity);
    payload[7] = 10 << 27;                                  // +20 dB, stock's top: held at 0 dB
    note(1, 53);
    CHECK(frame(1, payload) == unity);
    payload[7] = -1;
    note(1, 53);
    CHECK(frame(1, payload) == unity);
    payload[7] = 1 << 26;                                   // -6.02 dB: half
    note(1, 53);
    CHECK(frame(1, payload) == opxy::q31::multiply(unity, 1 << 30));
    payload[7] = 4244337;                                   // -30 dB, stock's floor: 10^-1.5 / 16 * 2^31
    note(1, 53);
    const opxy::i32 quiet = frame(1, payload);
    CHECK(quiet == opxy::q31::multiply(unity, 4244337 << 4) && quiet > 0 && quiet < unity / 30);
    // The pad's payload changes under a sounding note (a later note on the pad, a
    // step's lock or none): the note keeps its own level until it starts again.
    payload[7] = 1 << 27;
    CHECK(frame(1, payload) == quiet);
    note(2, 53);
    CHECK(frame(2, payload) == opxy::q31::multiply(3 << 16, 0x7fffffff));   // its owner is 3
    CHECK(frame(1, payload) == quiet);
    note(1, 53);
    CHECK(frame(1, payload) == unity);
    // padMix's sends: the note keeps its first block's send codes too, as the
    // mixer asks them after each block; a later payload is the next note's.
    payload[0] = kMarked;
    payload[6] = static_cast<opxy::i32>((5u << 25) | (6u << 28) | opxy::platform::kDrumPadReady);
    note(1, 53);
    frame(1, payload);
    CHECK(probe_drum_send(objects[1]) == (5u | 6u << 3));
    payload[6] = static_cast<opxy::i32>((2u << 25) | opxy::platform::kDrumPadReady);
    frame(1, payload);
    CHECK(probe_drum_send(objects[1]) == (5u | 6u << 3));
    note(1, 53);
    frame(1, payload);
    CHECK(probe_drum_send(objects[1]) == 2u);
    return 0;
}
"""


# The track filter (M3, drum_filter.hpp) on kit voices, against host doubles of stock's
# factory and objects: off leaves every sample as before; on, a note's first filtered
# block builds and resets the object in its voice index's slot, each block runs it
# between stock's 3-bit shift down and a saturating shift back up, with the track's
# record, the note's key and the envelope's last value; a type change rebuilds it.
FILTER_DRIVER = DRIVER[:DRIVER.index('int main() {')] + r"""
extern "C" opxy::u32 probe_drum_send(const void*);
bool block(unsigned id, opxy::i32* output) {
    reinterpret_cast<Render>(const_cast<void*>(table(id)[3]))(objects[id], output, nullptr, offsets);
    return true;
}
int main() {
    (void)kMarked;
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = 0x40000000;
    for (unsigned i = 0; i < 64; ++i) fixture_filter_envelope[i] = 1000 + static_cast<opxy::i32>(i);
    static opxy::i32 record[58];
    for (unsigned i = 0; i < 58; ++i) record[i] = 0x40000000;
    for (unsigned i = 0; i < 256; ++i) CHECK(objects[i] = probe_factory(i, i % 8));
    // M3 off: exactly the unfiltered voice, and no object is built.
    note(1, 60);
    opxy::i32 off[opxy::block_frames], on[opxy::block_frames];
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], off, nullptr, record);
    const opxy::i32 plain = opxy::q31::multiply(2 << 16, 0x40000000);
    for (auto sample : off) CHECK(sample == plain);
    CHECK(!fake_builds);
    // Another track's M3 is not this voice's (handle 1 plays track 1).
    fixture_filter_on[2] = 1; fixture_filter_type[2] = 10;
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    CHECK(!fake_builds && !std::memcmp(on, off, sizeof(on)));
    // M3 on, svf: built in slot 1 for track 1 and reset once; run with the
    // track's record, note 60's key and the envelope's last value, on the
    // block shifted down by 3; its output shifted back up.
    fixture_filter_on[1] = 1; fixture_filter_type[1] = 10;
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    FakeFilter& f = fake_pool[1];
    CHECK(fake_builds == 1 && f.type == 10 && f.index == 1 && f.track == 1 && f.resets == 1 && f.runs == 1);
    CHECK(f.record == record && f.envelope == 1063 && f.first == (2 << 16) >> 3);
    CHECK(f.key == opxy::q31::add(static_cast<opxy::i32>(60u * 0x00ffffffu), 0x007fffff));
    for (auto sample : on) CHECK(sample == opxy::q31::multiply(((2 << 16) >> 3) * 2 << 3, 0x40000000));
    // The next block runs the same object: no rebuild, no reset.
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    CHECK(fake_builds == 1 && f.resets == 1 && f.runs == 2);
    // A type change rebuilds and resets it in place; the shift back up
    // saturates a peak past full scale instead of wrapping.
    fixture_filter_type[1] = 17;
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    CHECK(fake_builds == 2 && f.type == 17 && f.resets == 2 && f.runs == 1);
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        CHECK(on[i] == opxy::q31::multiply((i & 1) ? opxy::q31::maximum : -opxy::q31::maximum - 1, 0x40000000));
    // A new note on the voice builds and resets it again, as stock's
    // constructor does for every synth note, and takes the new note's key.
    note(1, 72);
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    CHECK(fake_builds == 3 && f.resets == 3 && f.key == opxy::q31::add(static_cast<opxy::i32>(72u * 0x00ffffffu), 0x007fffff));
    // M3 off again: the unfiltered voice, the object left as it was.
    fixture_filter_on[1] = 0;
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    CHECK(!std::memcmp(on, off, sizeof(on)) && f.runs == 1 && fake_builds == 3);
    // A type stock has no filter for (18, dynamic): unfiltered, untouched by the shifts.
    fixture_filter_on[1] = 1; fixture_filter_type[1] = 18;
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    CHECK(!std::memcmp(on, off, sizeof(on)) && fake_builds == 3);
    // A voice index without a filter slot (128 and up): unfiltered.
    fixture_filter_type[1] = 16;
    note(129, 60);
    reinterpret_cast<Render>(const_cast<void*>(table(129)[3]))(objects[129], on, nullptr, record);
    for (auto sample : on) CHECK(sample == opxy::q31::multiply(130 << 16, 0x40000000));
    CHECK(fake_builds == 3);
    // A silent handle (its voice taken) renders silence without its filter.
    note(9, 60);
    note(17, 60);   // capacity two: handle 1's voice goes to this note
    reinterpret_cast<Render>(const_cast<void*>(table(1)[3]))(objects[1], on, nullptr, record);
    for (auto sample : on) CHECK(sample == 0);
    return 0;
}
"""


# A kit with pad controls and a drum_pad hook: the fields of a key the kit set up
# from the payload's word 6, neutral for any other key or no payload.
PAD_ENGINE = ENGINE.replace('#include "core.h"', '#include "core.h"\nextern opxy::i32 pad_values[2];\nextern unsigned pad_calls;', 1).replace(
    'inline const char* parameter_name', """inline void drum_pad(Voice&, Shared&, opxy::u32, const opxy::i32* values) {
    ++pad_calls;
    for (unsigned i = 0; i < 2; ++i) pad_values[i] = values[i];
}
inline bool pad_control_text(opxy::u32 field, opxy::i32 value, char* text) {
    text[0] = char('A' + field); text[1] = char('0' + value); text[2] = 0; return true;
}
inline const char* parameter_name""", 1)
PAD_DRIVER = DRIVER[:DRIVER.index('int main() {')].replace(
    'unsigned destroyed[257]', 'opxy::i32 pad_values[2];\nunsigned pad_calls;\nunsigned destroyed[257]') + r"""
extern "C" bool probe_drum_pad_text(opxy::u32, opxy::i32, char*);
int main() {
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = 0x7fffffff;
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    for (unsigned i = 0; i < 256; ++i) CHECK(objects[i] = probe_factory(i, i % 8));
    note(1, 53);
    opxy::i32 payload[8] = {};
    fixture_region = payload;
    // FILTER 5 bits signed at 16..20, SRR 4 at 21..24; the sends above are not the engine's.
    // Each note plays the pad controls of its first block's payload (a new note per value).
    payload[0] = kMarked;
    const opxy::u32 ready = opxy::platform::kDrumPadReady;
    payload[6] = static_cast<opxy::i32>(((32u - 3u) << 16) | (9u << 21) | (5u << 25) | (6u << 28) | ready);
    CHECK(rendered(1) == 2 && pad_calls == 1);
    CHECK(pad_values[0] == -3 && pad_values[1] == 9);
    payload[6] = static_cast<opxy::i32>((15u << 16) | (15u << 21) | ready);
    // A sounding note keeps its own: the payload is the pad's, which a later note replaces.
    CHECK(rendered(1) == 2 && pad_values[0] == -3 && pad_values[1] == 9);
    note(1, 53);
    CHECK(rendered(1) == 2 && pad_values[0] == 15 && pad_values[1] == 15);
    payload[6] = static_cast<opxy::i32>((16u << 16) | ready);
    note(1, 53);
    CHECK(rendered(1) == 2 && pad_values[0] == -16 && !pad_values[1]);
    // A key an SDK 1.9 kit marked, which never set it up: stock's default kit's
    // last key, an editor value with the same fields and its own low half, and
    // the fields with a low half of 0, as a preset restores them, are neutral.
    const opxy::i32 old_words[] = {0x26660000, opxy::i32(199 * 10737418 + 5368709),
                                   static_cast<opxy::i32>((15u << 16) | (15u << 21) | 0x1234u),
                                   static_cast<opxy::i32>((15u << 16) | (15u << 21))};
    for (opxy::i32 old_word : old_words) {
        payload[6] = old_word;
        note(1, 53);
        CHECK(rendered(1) == 2 && !pad_values[0] && !pad_values[1]);
    }
    // A pad the kit has not marked: whatever stock left in the word is neutral.
    payload[0] = opxy::i32(2u * 32537631u + 16268815u);
    payload[6] = static_cast<opxy::i32>(0x7a5a0000u | ready);
    note(1, 53);
    CHECK(rendered(1) == 2 && !pad_values[0] && !pad_values[1]);
    fixture_region = nullptr;
    pad_values[0] = 9;
    note(1, 53);
    CHECK(rendered(1) == 2 && !pad_values[0] && pad_calls == 10);
    // The stock header's caption comes from the engine.
    char text[16] = {};
    CHECK(probe_drum_pad_text(1, 5, text) && text[0] == 'B' && text[1] == '5');
    return 0;
}
"""


BENCH_ENGINE = """#include "core.h"
namespace probe {
inline void initialize(Voice& v, Shared&, opxy::u32 id) { v.owner = id; ++initialized[id]; }
inline void start(Voice&, Shared&, opxy::u32 id) { ++started[id]; }
inline void destroy(Voice&, Shared&, opxy::u32 id) { ++destroyed[id]; }
inline void drum_defaults(opxy::u32 key, opxy::u8* model, opxy::u8* raw) {
    *model = key % 12;
    for (unsigned i = 0; i < 8; ++i) raw[i] = 90;
}
inline void drum_select(Voice&, Shared&, opxy::u32, opxy::u32 model) { captured_model = model; }
inline const char* parameter_name(opxy::u32, opxy::u32) { return "parameter"; }
inline const char* model_name(opxy::u32) { return "model"; }
inline opxy::u32 parameter_count(opxy::u32) { return 8; }
inline void render(Voice&, Shared&, opxy::u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch) {
    for (unsigned i = 0; i < 8; ++i) captured[i] = parameters[i];
    captured_pitch[0] = pitch[0];
    ++bench_rendered[captured_model][static_cast<opxy::u32>(parameters[0]) >> 24];
    // Model 7 at level 127 runs far the longest, by the fixture's clock.
    if (captured_model == 7 && parameters[0] == 0x7fffffff) clock_value += 5000;
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = captured_model == 3 ? 0 : 65536;
}
}
"""
BENCH_DRIVER = r"""#include "voice.cpp"
#include <cstdio>
opxy::i32 captured[8];
opxy::u32 captured_model = 99;
opxy::u32 fixture_pitch[opxy::block_frames], captured_pitch[opxy::block_frames];
opxy::i32 fixture_amplitude[opxy::block_frames];
bool negative_output;
unsigned destroyed[257], initialized[257], started[257];
unsigned bench_rendered[12][128];
unsigned clock_value;
volatile opxy::i32 sdk_track_octaves[8];
extern "C" const char probe_engine_id[] = "probe";
const char* sdk_drum_title(opxy::u32, opxy::u32) { return "parameter"; }
extern "C" opxy::u32 sdk_benchmark_cycles() { return clock_value += 100; }
namespace opxy { namespace benchmark { void* allocate(u32) { return nullptr; } void release(void*) {} }}
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
int main() {
    // A run a report: the kit, 12 models at three levels, the choke, the ceiling.
    unsigned heaviest = 0;
    const opxy::benchmark::Stats kit = probe_benchmark(0);
    CHECK(kit.count == 1500 && kit.audible && kit.total_low > 0);
    CHECK(captured[0] == 90 << 24);   // the kit's own keys, saved at their defaults, play as those
    for (unsigned model = 0; model < 12; ++model)
        for (unsigned level = 0; level < 3; ++level) {
            const unsigned before = bench_rendered[model][level == 0 ? 64 : level == 1 ? 0 : 127];
            const opxy::benchmark::Stats one = probe_benchmark(1 + model * 3 + level);
            CHECK(one.count == 1500 && one.audible);   // a silent model still reports its cost
            if (model == 7 && level == 2) heaviest = one.total_low;
            CHECK(captured_model == model);
            CHECK(bench_rendered[model][level == 0 ? 64 : level == 1 ? 0 : 127] == before + 1500);
        }
    CHECK(benchmark_model == 7 && kBenchmarkLevels[benchmark_level] == 127);
    // The choke: two voices on one key, the heaviest model, the old one fading and freed.
    const unsigned freed = destroyed[256] + destroyed[255];
    const opxy::benchmark::Stats choke = probe_benchmark(12 * 3 + 1);
    CHECK(choke.count == 1500 && choke.audible && captured_model == 7);
    CHECK(destroyed[256] + destroyed[255] > freed + 100);
    // The ceiling: every voice at once, far above one.
    const opxy::benchmark::Stats ceiling = probe_benchmark(12 * 3 + 2);
    CHECK(ceiling.count == 1500 && captured_model == 7);
    CHECK(ceiling.total_low > 3 * heaviest);   // all four voices sound, on keys of their own: a same-key choke would leave one
    CHECK(captured_pitch[0] == (25480118u >> 16) << 16);
    // Every voice is back: a real note after the workload takes a free slot.
    for (const auto& slot : voices) CHECK(!slot.owner);
    // Every voice ran the track filter at its costliest type, the ladder, in a
    // pool slot of its own from 127 down, with M3's words from the record.
    CHECK(fake_runs > 1500 && fake_pool[127].type == 16 && fake_pool[124].type == 16 && fake_pool[124].runs > 0);
    CHECK(fake_pool[127].record && fake_pool[127].record[17] == 0x60000000 && fake_pool[127].envelope == 0x40000000);
    return 0;
}
"""


FILTER_STOCK = {
    'reinterpret_cast<const volatile opxy::u8*>(0x11b01dc8)': 'fixture_filter_on',
    'reinterpret_cast<const volatile opxy::u32*>(0x84042700)': 'fixture_filter_type',
    'reinterpret_cast<const volatile opxy::i32*>(0x11903500)': 'fixture_filter_envelope',
    'reinterpret_cast<opxy::platform::DrumFilterFactory>(0x8d505220)': '&fixture_filter_factory',
}


class DrumVoices(unittest.TestCase):
    def run_adapter(self, engine, driver, capacity, **resources):
        definition = copy.deepcopy(fixture('probesynth').definition)
        definition['id'] = 'probe'
        definition['resources'].update(voiceBytes=4, voiceAlignment=4, voiceCapacity=capacity,
                                       sharedBytes=0, modelCount=12, **resources)
        machine = SimpleNamespace(kind='drum', definition=definition, prefix='probe', native_name='probe')
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = generate_voice(folder, machine)
            # The actual +12 ABI transports its payload directly; no global
            # payload-pointer substitution is needed to execute the adapter.
            generated = source.read_text(encoding='utf-8')
            self.assertNotIn('0x0803b9c4', generated)
            self.assertIn('const bool kChoke = %s;' % ('true' if resources.get('chokeSameKey') else 'false'), generated)
            native_pitch = 'reinterpret_cast<const opxy::u32*>(opxy::platform::kPitchIncrements)'
            self.assertEqual(generated.count(native_pitch), 1)
            native_amplitude = 'reinterpret_cast<const opxy::i32*>(0x11903400)'
            self.assertEqual(generated.count(native_amplitude), 1)
            generated = generated.replace(native_pitch, 'fixture_pitch').replace(native_amplitude, 'fixture_amplitude')
            # The track filter's stock state and factory (drum_filter.hpp), each read once.
            for native, double in FILTER_STOCK.items():
                self.assertEqual(generated.count(native), 1, native)
                generated = generated.replace(native, double)
            source.write_text(generated, encoding='utf-8')
            for name, content in (('core.h', CORE), ('engine.cpp', engine), ('test.cpp', driver)):
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

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_native_parameter_transport_and_voice_lifecycle(self):
        # Without chokeSameKey or a finished hook: handles 0 and 200 share track
        # zero and key 53 and both keep sounding.
        self.run_adapter(ENGINE, DRIVER, 2)

    def test_a_kit_is_in_the_control_and_lfo_tables(self):
        # Staging's table declares by kind: a kit needs its engine ID, ready check and LFO row there too.
        from dataclasses import replace
        from tooling.adapters import generate_synth_controls
        base = fixture('probesynth')
        definition = dict(base.definition, resources=dict(base.definition['resources'], modelCount=2))
        drum = replace(base, kind='drum', id='probedrum', definition=definition)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(generate_synth_controls(directory, [drum])).read_text(encoding='utf-8')
        for line in (f'{drum.prefix}_record_ready() __asm__', f'extern const char {drum.prefix}_engine_id[] __asm__',
                     f'{{{drum.prefix}_engine_id, {drum.prefix}_record_ready, '):
            self.assertIn(line, source)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_profiling_workload_drives_every_model_through_the_adapter(self):
        self.run_adapter(BENCH_ENGINE, BENCH_DRIVER, 4, chokeSameKey=1)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_pad_mix_cuts_each_voice_by_its_pads_level(self):
        self.run_adapter(ENGINE, MIX_DRIVER, 2, padMix=1)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_pad_controls_reach_the_engine_for_a_key_the_kit_set_up_only(self):
        self.run_adapter(PAD_ENGINE, PAD_DRIVER, 2, padMix=1, padControls=[
            dict(label='FILTER', bits=5, signed=True), dict(label='SRR', bits=4)])

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_profiling_workload_runs_a_pad_mix_kit_at_its_heaviest_pad_settings(self):
        self.run_adapter(BENCH_ENGINE, BENCH_DRIVER, 4, chokeSameKey=1, padMix=1, padControls=[
            dict(label='FILTER', bits=5, signed=True), dict(label='SRR', bits=4)])

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_track_filter_runs_on_kit_voices_and_off_leaves_them_unchanged(self):
        self.run_adapter(ENGINE, FILTER_DRIVER, 2)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_same_key_choke_fades_the_old_voice_and_finished_frees_it(self):
        self.run_adapter(CHOKE_ENGINE, CHOKE_DRIVER, 4, chokeSameKey=1)


if __name__ == '__main__':
    unittest.main()
