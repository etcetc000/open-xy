// The idle cut on the host: its arithmetic (idle_cut.hpp) and the real hook
// (idle_cut.cpp) against stock-shaped voices, drum sampler objects and samples.
// Stock fields hold 32-bit addresses; host_stock_address maps them to host memory.
// Prints "ok".
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "idle_cut.hpp"
#include "stock.hpp"

using namespace opxy;
using namespace opxy::platform;
namespace cut = opxy::platform::idle_cut;

extern "C" {
int sdk_idle_render(unsigned char* voice, i32* output) __asm__("sdk_idle_render");
i32 sdk_idle_floor() __asm__("sdk_idle_floor");
bool sdk_idle_register(const void* methods, int (*settled)(void*)) __asm__("sdk_idle_register");
}

namespace {
void require(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        std::exit(1);
    }
}

// --- Stock doubles ---
i32 patched_floor = 3000000;  // the setting's floor; 0: stock (stage 0 below its own)
bool patched = true;
int chooser_done;
u32 chooser_calls, kills, probes;
u16 frame_index;
unsigned char track_words[64];
std::vector<unsigned char*> killed;

void envelope(u32* e, u32, u32, i32, u32) {
    ++probes;
    if (e[4] == 4 && static_cast<i32>(e[1]) < (patched ? patched_floor : 1073742)) e[4] = patched ? 5 : 0;
}
int chooser(unsigned char*, i32*) {
    ++chooser_calls;
    return chooser_done;
}
void kill(unsigned char* v) {
    ++kills;
    killed.push_back(v);
    v[0x51] = 0;
    *reinterpret_cast<u32*>(v + 0x24) = 6;
}
unsigned char* track_params(u32 object, u32) { return object == 0x84eab39c ? track_words : nullptr; }

// Host memory under 32-bit addresses: each region from its base.
struct Mapped { u32 base; unsigned char* host; u32 size; };
std::vector<Mapped> mapped;
u32 next_base = 0x10000000;
u32 map(void* host, u32 size) {
    const u32 base = next_base;
    next_base += (size + 0xfff) & ~0xfffu;
    mapped.push_back({base, static_cast<unsigned char*>(host), size});
    return base;
}
void put(unsigned char* p, u32 offset, u32 value) { std::memcpy(p + offset, &value, 4); }
// A play mode's word, in the middle of its sixth of the range, as stock reads it.
u32 mode_word(u32 mode) { return static_cast<u32>((2ull * mode + 1) * 0x80000000ull / 12); }
}

Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d50e110) return reinterpret_cast<Address>(&envelope);
    if (address == 0x8d51bbaa) return reinterpret_cast<Address>(&chooser);
    if (address == 0x8d51a998) return reinterpret_cast<Address>(&kill);
    if (address == 0x8d518d94) return reinterpret_cast<Address>(&track_params);
    if (address == 0x84a50b98) return reinterpret_cast<Address>(&frame_index);
    for (const Mapped& m : mapped)
        if (address >= m.base && address < m.base + m.size) return reinterpret_cast<Address>(m.host + (address - m.base));
    std::fprintf(stderr, "Unmapped stock address %08x\n", static_cast<unsigned>(address));
    std::abort();
}

namespace {
// --- A stock voice, its drum sampler and its sample ---
const i32 kUnity = 1 << 27;  // a pad's level of 0 dB (Q27)
struct Sample {
    std::vector<i16> pcm;
    unsigned char object[0x40];
    u32 pcm_address, address;
    Sample(const std::vector<i16>& data, bool stereo) : pcm(data), object() {
        pcm_address = map(pcm.data(), static_cast<u32>(pcm.size() * 2));
        put(object, 0x08, pcm_address);
        put(object, 0x30, static_cast<u32>(pcm.size() >> (stereo ? 1 : 0)));
        object[0x3c] = stereo ? 1 : 0;
        address = map(object, sizeof object);
    }
};
struct Voice {
    unsigned char voice[0x140];
    unsigned char sampler[0x50];
    unsigned char region[0x20];
    u32 frame;  // the playback position, in frames
    explicit Voice(const Sample* s, u32 mode = 1) : voice(), sampler(), region(), frame(0) {
        put(sampler, 0, 0x8404d648);
        if (s) put(sampler, 0x04, s->address);
        put(sampler, 0x34, kUnity);
        put(sampler, 0x3c, 1);
        put(region, 0x0c, mode_word(mode));
        put(region, 0x1c, kUnity);
        put(voice, 0x10, map(region, sizeof region));
        put(voice, 0x24, 3);
        voice[0x4e] = 1;
        voice[0x50] = 60;
        voice[0x51] = 1;
        put(voice, 0x94, map(sampler, sizeof sampler));
    }
    void at(u32 f) {
        frame = f;
        put(sampler, 0x18, f << 11);
    }
    void state(u32 s) { put(sampler, 0x3c, s); }
    u32 count() const { u32 c; std::memcpy(&c, sampler + 0x40, 4); return c; }
    // One block: the hook after stock's render, which moved the position and
    // counted the block.
    bool render(u32 frames = 64) {
        put(sampler, 0x40, count() + 1);
        at(frame + frames);
        const u32 before = kills;
        i32 out[128];
        sdk_idle_render(voice, out);
        return kills != before;
    }
};
void next_block() { ++frame_index; }
bool block(Voice& v, u32 frames = 64) {
    next_block();
    return v.render(frames);
}

// A one-shot hit: full scale for `loud` frames, then a decay to silence, as a
// cymbal sample's tail.
std::vector<i16> decaying(u32 frames, u32 loud, u32 half_life) {
    std::vector<i16> s(frames);
    for (u32 i = 0; i < frames; ++i) {
        double a = i < loud ? 30000.0 : 30000.0;
        if (i >= loud) for (u32 k = 0; k < (i - loud) / half_life; ++k) a /= 2;
        s[i] = static_cast<i16>((i & 1) ? a : -a);
    }
    return s;
}

// The signature's sample positions, spread(last, k) = last * k / 7, against the 64-bit product: every k at
// the bounds of last (0 up, 2^32 - 1 down, each side of every multiple of 7 near both ends) and a spread of
// others. The identity behind it (last = 7q + r) holds for every u32; these are where an overflow would show.
void positions() {
    std::vector<u32> lasts;
    for (u32 v = 0; v < 4096; ++v) { lasts.push_back(v); lasts.push_back(0xffffffffu - v); }
    for (unsigned long long v = 7; v < (1ull << 32); v = v * 3 + 1) { lasts.push_back(u32(v - 1)); lasts.push_back(u32(v)); }
    u32 x = 12345;
    for (u32 i = 0; i < 200000; ++i) { x = x * 1664525u + 1013904223u; lasts.push_back(x); }
    for (u32 last : lasts)
        for (u32 k = 0; k < 8; ++k)
            require(cut::spread(last, k) == u32((unsigned long long)last * k / 7), "a sample position differs from the 64-bit form");
}

void arithmetic() {
    positions();
    // The follower: the note's peak, kHold blocks under the threshold while settled.
    cut::Follower f;
    cut::restart(f);
    const i32 floor = 67908514;  // -30 dB
    require(!cut::follow(f, 1 << 28, true, floor), "the peak block cut");
    for (u32 b = 0; b < cut::kHold - 1; ++b) require(!cut::follow(f, 1000, true, floor), "cut before kHold blocks");
    require(cut::follow(f, 1000, true, floor), "not cut after kHold blocks");
    cut::restart(f);
    cut::follow(f, 1 << 28, false, floor);
    for (u32 b = 0; b < 100; ++b) require(!cut::follow(f, 0, false, floor), "an unsettled voice cut");
    // An LFO dip on a settled voice that rises again before kHold blocks resets it.
    for (u32 b = 0; b < cut::kHold - 1; ++b) cut::follow(f, 1000, true, floor);
    require(!cut::follow(f, 1 << 26, true, floor) && f.quiet == 0, "a voice that rose again kept its count");
    require(!cut::follow(f, 0, true, 0), "cut with the setting off");
    // The documented hold: 8 blocks (12 ms) under the threshold.
    require(cut::kHold == 8, "the hold is not the documented 8 blocks");
    // At the threshold exactly counts as under it; one above does not.
    {
        const i32 at = q31::multiply(1 << 28, floor);
        cut::restart(f);
        cut::follow(f, 1 << 28, true, floor);
        for (u32 b = 1; b < cut::kHold; ++b) require(!cut::follow(f, at, true, floor), "cut early at the threshold");
        require(cut::follow(f, at, true, floor), "a block at the threshold was not under it");
        cut::restart(f);
        cut::follow(f, 1 << 28, true, floor);
        for (u32 b = 0; b < 2 * cut::kHold; ++b) require(!cut::follow(f, at + 1, true, floor), "a block above the threshold counted");
    }
    // The peak of a block.
    const i32 x[4] = {3, -7, 5, -0x7fffffff - 1};
    require(cut::peak(x, 3) == 7 && cut::peak(x, 4) == 0x7fffffff, "block peaks");
    // The floor, probed: the setting's (one below the level it ends a release
    // at), or none from stock's routine.
    patched = true;
    require(cut::probe_floor([](i32 level) {
        u32 e[8] = {0, static_cast<u32>(level), 0, 0, 4, 0, 0, 0};
        envelope(e, 0, 0, 0, 0);
        return e[4];
    }) == patched_floor - 1, "the probed floor");
    patched = false;
    require(cut::probe_floor([](i32 level) {
        u32 e[8] = {0, static_cast<u32>(level), 0, 0, 4, 0, 0, 0};
        envelope(e, 0, 0, 0, 0);
        return e[4];
    }) == 0, "stock's floor taken as a setting");
    // A routine whose release at level 0 does not enter stage 5 is taken as stock, whatever it does above.
    require(cut::probe_floor([](i32 level) { return level > 0 && level < 1000 ? 5u : 0u; }) == 0,
            "a floor probed where level 0 does not end in stage 5");
    patched = true;
    // Play modes as stock reads them.
    for (u32 m = 0; m < 6; ++m) require(cut::sampler_mode(0x40000000, static_cast<i32>(mode_word(m))) == m, "the pad's mode");
    require(cut::sampler_mode(0x7fffffff, 0) == 5 && cut::sampler_mode(0, static_cast<i32>(mode_word(3))) == 0, "the track's mode");
    // Tables: chunk maxima and from each chunk to the end.
    {
        std::vector<i16> s(5000, 0);
        s[10] = 100;
        s[3000] = -2000;
        s[4999] = 50;
        cut::Table t;
        cut::begin(t, s.data(), 5000, 0, 0);
        require(t.shift == cut::kMinShift && t.chunks == 20, "table chunks");
        u32 rounds = 0;
        while (!t.complete && rounds < 100) {
            cut::scan(t, 300);
            ++rounds;
        }
        require(t.complete && rounds == 17, "scanned within its budget");
        require(t.chunk_max[0] == 100 && t.chunk_max[11] == 2000 && t.chunk_max[19] == 50, "chunk maxima");
        require(t.suffix_max[0] == 2000 && t.suffix_max[11] == 2000 && t.suffix_max[12] == 50, "suffix maxima");
        // A long sample takes larger chunks, within kChunks.
        cut::Table l;
        std::vector<i16> big(1048576, 0);
        cut::begin(l, big.data(), 1048576, 0, 0);
        require(l.chunks <= cut::kChunks && l.shift == 11, "a long sample's chunks");
    }
}

void hook() {
    put(track_words, 0x0c, 0x40000000);
    // The setting off: stock's chooser only, nothing scanned or cut.
    patched = false;
    {
        Sample s(decaying(20000, 512, 256), false);
        Voice v(&s);
        v.state(2);
        for (u32 b = 0; b < 400; ++b) require(!block(v), "cut with the setting off");
        require(chooser_calls == 400, "stock's chooser was not called");
        require(sdk_idle_floor() == 0, "a floor without the setting");
    }
    // The floor is probed once, on the first voice it is needed for (in the
    // fixture, stock's -66 dB answer above already set it): a fresh probe here
    // would need a fresh process, so the rest runs at the setting's floor by
    // restarting from the patched double.
}

}  // namespace

// The rest of the hook needs the setting from its first probe; the fixture runs
// as two processes (main's argument picks one).
namespace {
void hook_with_setting() {
    put(track_words, 0x0c, 0x40000000);  // the track leaves the play mode to the pad
    patched = true;
    patched_floor = 67908515;  // -30 dB
    // A finished render is passed on, untouched.
    {
        Sample s(decaying(20000, 512, 64), false);
        Voice v(&s);
        chooser_done = 1;
        i32 out[128];
        require(sdk_idle_render(v.voice, out) == 1 && kills == 0, "a finished voice was not passed on");
        chooser_done = 0;
        require(sdk_idle_floor() == 67908514, "the setting's floor");
    }
    // A one-shot hit decaying into silence: cut once all it can still play is
    // 30 dB (and 6 more) below what it played, not before.
    {
        Sample s(decaying(40000, 512, 400), false);  // halves every 400 frames
        Voice v(&s, 1);
        u32 cut_at = 0;
        for (u32 b = 0; b < 600 && !cut_at; ++b)
            if (block(v)) cut_at = v.frame;
        require(cut_at, "a decayed one-shot was never cut");
        // 30000 / 2^n <= 30000 * 0.0316 / 2: n >= 6 halvings, frame 512 + 2400 at the
        // earliest; the chunk holding it must be past it entirely.
        require(cut_at >= 512 + 6 * 400, "a one-shot cut while it could still be loud");
        require(cut_at <= 512 + 7 * 400 + 2 * 256 + 64 * 4, "a one-shot cut late");
        // Its fade: stock's kill. Later blocks while it fades (stage 6, then 7)
        // are left alone, and so is a steal that waits on it (stage 6, gate on).
        const u32 k = kills;
        for (u32 b = 0; b < 3; ++b) require(!block(v), "a fading voice cut again");
        put(v.voice, 0x24, 7);
        require(!block(v), "a voice past its fade cut again");
        put(v.voice, 0x24, 6);
        v.voice[0x51] = 1;
        require(!block(v) && kills == k, "a voice fading for a steal cut");
        // Retriggered during the fade: stock starts the note again (stage 1, gate
        // on, its count from 0, the position at the start): a new note, with no
        // reference yet, so it plays.
        put(v.voice, 0x24, 1);
        put(v.sampler, 0x40, 0);
        v.at(0);
        for (u32 b = 0; b < 20; ++b) require(!block(v), "a retriggered note cut");
    }
    // A silent gap, then a loud hit: never cut in the gap.
    {
        std::vector<i16> d(30000, 0);
        for (u32 i = 0; i < 600; ++i) d[i] = static_cast<i16>((i & 1) ? 20000 : -20000);
        for (u32 i = 20000; i < 20600; ++i) d[i] = static_cast<i16>((i & 1) ? 25000 : -25000);
        Sample s(d, false);
        Voice v(&s, 2);
        u32 cut_at = 0;
        for (u32 b = 0; b < 460 && !cut_at; ++b)
            if (block(v)) cut_at = v.frame;
        require(cut_at > 20600, "a voice cut in a gap before a loud hit");
    }
    // Stereo: either channel counts.
    {
        std::vector<i16> d(2 * 20000, 0);
        for (u32 i = 0; i < 600; ++i) d[2 * i] = 20000;
        d[2 * 15000 + 1] = 20000;  // a click on the right channel only
        Sample s(d, true);
        Voice v(&s, 1);
        u32 cut_at = 0;
        for (u32 b = 0; b < 300 && !cut_at; ++b)
            if (block(v)) cut_at = v.frame;
        require(cut_at > 15000, "a stereo voice cut before its right channel's click");
    }
    // A looped sample while held (loop mode, state 1): never cut, though what it
    // plays is quiet; released (state 2) it plays to its end and goes once quiet.
    {
        Sample s(decaying(20000, 512, 256), false);
        Voice v(&s, 3);
        put(v.sampler, 0x28, 0);  // the loop goes back to the start
        for (u32 b = 0; b < 400; ++b) {
            require(!block(v), "a held loop cut");
            if (v.frame >= 19000) v.at(0);
        }
        v.state(2);
        v.at(10000);
        require(block(v), "a released loop in its quiet tail was not cut");
    }
    // A loop that goes back past a loud start: never cut while it loops, even
    // when the position is in a quiet part. (Held: state 1; checked through the
    // rule above. Released into the loud start again is a new pass: still the
    // end decides.)
    // A pad retriggered after one block with its start moved later: a new note,
    // though stock's count does not go back past what was seen. Its peak is its
    // own quiet part, not the loud start it never played, so it is not cut.
    {
        std::vector<i16> d(9000, 0);
        for (u32 i = 0; i < 600; ++i) d[i] = static_cast<i16>((i & 1) ? 20000 : -20000);
        for (u32 i = 600; i < 8000; ++i) d[i] = static_cast<i16>((i & 1) ? 300 : -300);
        Sample s(d, false);
        Voice v(&s, 1);
        require(!block(v), "the first note cut");
        put(v.sampler, 0x40, 0);
        put(v.sampler, 0x20, 1000u << 11);
        v.at(1000);
        for (u32 b = 0; b < 30; ++b) require(!block(v), "a retriggered note judged against a start it never played");
    }
    // Gate mode: held never cut; released, cut once quiet.
    {
        Sample s(decaying(20000, 512, 256), false);
        Voice v(&s, 0);
        for (u32 b = 0; b < 250; ++b) require(!block(v), "a held gate-mode note cut");
        v.state(2);
        require(block(v), "a released gate-mode note in silence was not cut");
    }
    // Reverse playback is never cut.
    {
        Sample s(decaying(20000, 512, 256), false);
        Voice v(&s, 1);
        v.sampler[0x45] = 1;
        for (u32 b = 0; b < 300; ++b) require(!block(v), "a reversed note cut");
    }
    // Stock Drumline (the drum byte, another method table) and a voice whose
    // sample is gone are never cut.
    {
        Sample s(decaying(20000, 512, 256), false);
        Voice v(&s, 1);
        put(v.sampler, 0, 0x8404ce3c);
        for (u32 b = 0; b < 300; ++b) require(!block(v), "a Drumline voice cut");
        Voice gone(nullptr, 1);
        for (u32 b = 0; b < 300; ++b) require(!block(gone), "a voice without a sample cut");
    }
    // Another sample loaded where one was, as long: its table is scanned again.
    {
        std::vector<i16> quiet = decaying(20000, 512, 128);
        Sample s(quiet, false);
        Voice v(&s, 1);
        u32 first_cut = 0;
        for (u32 b = 0; b < 300 && !first_cut; ++b)
            if (block(v)) first_cut = v.frame;
        require(first_cut, "the first sample was not cut");
        // The same place now holds a sample loud at its end.
        for (u32 i = 0; i < 20000; ++i) s.pcm[i] = static_cast<i16>(i > 19000 ? 30000 : (i < 512 ? 30000 : 0));
        Voice w(&s, 1);
        u32 cut_at = 0;
        for (u32 b = 0; b < 320 && !cut_at; ++b)
            if (block(w)) cut_at = w.frame;
        require(!cut_at || cut_at > 19000, "a new sample in an old one's place used its table");
    }
    // The scan stays within its budget a block, shared by every voice.
    {
        std::vector<i16> d(200000, 1000);
        Sample s(d, false);
        Voice a(&s, 1), b(&s, 1);
        next_block();
        a.render();
        b.render();
        // 200000 samples at 1024 a block: not complete after a block or two.
        next_block();
        a.render();
        require(!a.render(), "scanned past its budget");
    }
    // SDK synths: a registered machine's voice, once released, when it says so.
    {
        static unsigned char handle[16];
        static int answer;
        put(handle, 0, 0x9000aa00);
        unsigned char voice[0x140] = {};
        put(voice, 0x24, 4);
        voice[0x50] = 60;
        voice[0x51] = 1;  // held
        put(voice, 0x94, map(handle, sizeof handle));
        require(sdk_idle_register(reinterpret_cast<const void*>(static_cast<__UINTPTR_TYPE__>(0x9000aa00)),
                                  [](void*) { return answer; }), "registration refused");
        require(sdk_idle_register(reinterpret_cast<const void*>(static_cast<__UINTPTR_TYPE__>(0x9000aa00)),
                                  [](void*) { return 0; }), "registered twice");
        answer = 1;
        i32 out[128];
        const u32 k = kills;
        next_block();
        sdk_idle_render(voice, out);
        require(kills == k, "a held synth note cut (an LFO dip never cuts a held note)");
        voice[0x51] = 0;
        answer = 0;
        sdk_idle_render(voice, out);
        require(kills == k, "a released note its machine does not let go of cut");
        answer = 1;
        sdk_idle_render(voice, out);
        require(kills == k + 1, "a released, settled synth note was not cut");
        // An unregistered machine is never asked.
        put(handle, 0, 0x9000bb00);
        put(voice, 0x24, 4);
        sdk_idle_render(voice, out);
        require(kills == k + 1, "an unregistered machine's voice cut");
    }
}
}

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "setting")) {
        hook_with_setting();
    } else {
        arithmetic();
        hook();
    }
    std::printf("ok\n");
    return 0;
}
