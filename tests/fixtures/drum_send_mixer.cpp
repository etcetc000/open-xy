// Per-key sends: drum_send.cpp's hooks inside a host model of stock 1.2.1's
// mixer (8d510760), as drum_send_hooks.s places them. The model follows the
// stock instructions: the voice loop's saturating add of each rendered voice
// into the track's 128-word stereo sum (8d51c7b0..8d51c7d8), the track gain's
// in-place ramp over that sum (8d510e60..8d510e98) and its hook (8d510ea8), then
// the main mix at a constant gain (8d511038..8d511084), FX I at a gain stepped
// every word (8d511088..8d5110c8; on the device the step is 0, 8d510d74) and FX
// II at a constant gain (8d5110ca..8d51110e), then the own-amount hook
// (8d511116), then the dry: D = L - G1 (1 - d1) - G2 (1 - d2) from the send
// effects' dry queries (8d510d0a..8d510e44), times the track's dry word,
// smoothed, into the main out (8d511122..8d51117e). A skipped track (8d51237a)
// mixes nothing.
// The test substitutes host buffers for stock's 0x11903600, 0x11b01548, the
// send buses 0x84caa140 and 0x84caa340, the send slots' effect objects
// (0x11b01dc0), the track records (0x84eab39c) and the dry words (0x84c69700).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <opxy/q31.hpp>
// The stock buffers drum_send.cpp names, as the test substitutes them.
extern opxy::i32 host_sum[], host_voice[], host_fx1[], host_fx2[], host_dry_words[];
extern void* host_effects[];
extern unsigned char host_records[];
// A stock dbox voice's method table (0x8404ce3c) and its object's index, as
// the test substitutes them for stock's addresses.
extern const void* host_dbox_table[];
opxy::u32 host_dbox_index(const void* object);
#include "drum_send.cpp"

using opxy::i32;
using opxy::u32;
namespace q31 = opxy::q31;

i32 host_sum[kDrumSendWords], host_voice[kDrumSendWords], host_fx1[kDrumSendWords], host_fx2[kDrumSendWords];
i32 host_dry_words[16];
void* host_effects[2];
alignas(8) unsigned char host_records[16 * 0xe8];

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

namespace {
// A kit's voices as the hook sees them: a method table and both codes per handle.
const void* kit_table[11];
const void* stock_table[11];
struct Handle { const void* const* vtable; u32 codes; };
u32 kit_codes(const void* handle) { return static_cast<const Handle*>(handle)->codes; }
}
const DrumSendKit sdk_drum_send_kits[] = {{kit_table, kit_codes}};
const u32 sdk_drum_send_kit_count = 1;

namespace {
u32 host_dbox_index_of(const void* handle);
}
const void* host_dbox_table[11];
u32 host_dbox_index(const void* object) { return host_dbox_index_of(object); }

namespace {
// Each code's knob position, (c - 1) / 6 for codes 1..7, in Q31, and stock's
// curve for a send's knob position as its engine computes it (FX I
// 8d510d7a..8d510daa, FX II 8d510e0a): kDrumSendGains is these, worked out.
const i32 kDrumSendPositions[kDrumSendCodes] = {
    0, 0, 357913941, 715827883, 1073741824, 1431655765, 1789569707, q31::maximum};
i32 drum_send_curve(u32 send, i32 position) {
    if (send) return position;
    return q31::add(q31::multiply(q31::multiply(position, position), 1717986918), q31::multiply(position, 429496729));
}
// FX I's code in bits 0..2, FX II's in 3..5, as drum_send_codes gives them.
// 0 follows the track (TRK); 1 is dry; 7 is a full send of the key's own.
u32 pair(u32 fx1, u32 fx2) { return fx1 | fx2 << kDrumSendCodeBits; }

// A stock voice record: its machine object at +0x94 (8d51b53e).
struct StockVoice { unsigned char bytes[0x94 + sizeof(void*)]; Handle handle; i32 audio[kDrumSendWords]; };
void bind(StockVoice& voice, bool kit, u32 codes) {
    voice.handle.vtable = kit ? kit_table : stock_table;
    voice.handle.codes = codes;
    const void* object = &voice.handle;
    std::memcpy(voice.bytes + 0x94, &object, sizeof object);
}
// A stock Drumline (dbox) voice: the dbox method table, its object's index
// held where a kit's handle holds its codes.
void bind_dbox(StockVoice& voice, u32 index) {
    bind(voice, false, index);
    voice.handle.vtable = host_dbox_table;
}
u32 host_dbox_index_of(const void* handle) { return static_cast<const Handle*>(handle)->codes; }

// The engine's gains for one track: main, FX I's start and step, FX II's, the
// level ([FP-0x68]) both sends are scaled from, and the track gain's ramp: the
// gain its smoother held before the block ([s + 4], or the target on its first
// block) and its step (8d510e4a..8d510e5e). Then for the dry: FX II's knob
// (record word 23, from which the hook works FX II's gain out as 8d510e0a does:
// tests that set it set fx2 to level x k2), what the effects on the two sends
// answer to their dry query, the track's dry word, and which slots are empty
// (bit 0 FX I's, bit 1 FX II's). Left 0, the dry word mutes the dry.
struct Gains { i32 main, fx1, step, fx2, level, track, track_step, k2, dry1, dry2, x; u32 empty; };
// Track gains: engine volume 50% x the drum group level's reset value (0.1375),
// the preset default, held; and a smoother part way from 0.125 down to it.
const i32 kTrackHeld = 147639501, kTrackFrom = 0x10000000, kTrackStep = (kTrackHeld >> 8) - (kTrackFrom >> 8);
// The track gain of frame `frame` (0..63): the start stepped frame + 1 times.
i32 track_gain(const Gains& g, u32 frame) {
    i32 gain = g.track;
    for (u32 n = 0; n <= frame; ++n) gain = q31::add(gain, g.track_step);
    return gain;
}
struct Buses { i32 main[kDrumSendWords], fx1[kDrumSendWords], fx2[kDrumSendWords], dry[kDrumSendWords]; };

// A send slot's effect: method word 7 is its dry query, which must be asked
// with the send track's record, as the engine asks it (8d510d44..8d510d5e).
struct FakeEffect { const void* const* table; i32 dry; u32 slot; };
i32 fake_dry(void* object, const void* record) {
    const FakeEffect* effect = static_cast<const FakeEffect*>(object);
    CHECK(record == host_records + (14 + effect->slot) * 0xe8);
    return effect->dry;
}
const void* fake_effect_table[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                    reinterpret_cast<const void*>(&fake_dry)};
// Stock's dry smoother per track ([FP-0x34]: step, current, shift, reset flag),
// one set for the stock model and one for the hooked mixer, so the two evolve
// alike; stock starts each with shift 8 and the flag set (8d50e60e).
struct DrySmoother { i32 step, current; int16_t shift; uint8_t reset; };
DrySmoother host_smoothers[2][8];
void reset_smoothers() {
    for (u32 side = 0; side < 2; ++side)
        for (u32 t = 0; t < 8; ++t) host_smoothers[side][t] = DrySmoother{0, 0, 8, 1};
}
template <typename T> void put(unsigned char* at, T value) { std::memcpy(at, &value, sizeof value); }
// Stock's dry for the track's gains (8d510d0a..8d510e44).
i32 stock_dry(const Gains& g) {
    const i32 w1 = (g.empty & 1) ? 0 : drum_send_subtract(q31::maximum, g.dry1);
    const i32 w2 = (g.empty & 2) ? 0 : drum_send_subtract(q31::maximum, g.dry2);
    return drum_send_subtract(drum_send_subtract(g.level, q31::multiply(g.fx1, w1)), q31::multiply(g.fx2, w2));
}

u32 seed = 12345;
i32 noise() { seed = seed * 1664525u + 1013904223u; return static_cast<i32>(seed); }

// One track of one block. `hooks` places the SDK's calls; `skip` takes stock's
// path that leaves the track out of every bus. `start` is what stock's sum
// holds before the first voice.
void mix(Buses& buses, u32 track, StockVoice* voices, u32 count, const i32* start, const Gains& g, bool hooks, bool skip) {
    if (hooks) sdk_drum_send_begin();                         // 8d510bea
    for (u32 i = 0; i < kDrumSendWords; ++i) host_sum[i] = start[i];
    for (u32 v = 0; v < count; ++v) {
        std::memcpy(host_voice, voices[v].audio, sizeof host_voice);   // the renderer's output
        if (hooks) sdk_drum_send_voice(voices[v].bytes, track);       // 8d51c7b2
        for (u32 i = 0; i < kDrumSendWords; ++i) host_sum[i] = q31::add(host_sum[i], host_voice[i]);
    }
    if (skip) return;
    // 8d510e60..8d510e98: stock scales its sum in place, frame n at the track
    // gain stepped n + 1 times; 8d510ea8 then sees where the ramp ended (R0) and
    // its step (R2).
    i32 ramp = g.track;
    for (u32 f = 0; f < opxy::block_frames; ++f) {
        ramp = q31::add(ramp, g.track_step);
        host_sum[2 * f] = q31::multiply(ramp, host_sum[2 * f]);
        host_sum[2 * f + 1] = q31::multiply(ramp, host_sum[2 * f + 1]);
    }
    if (hooks) sdk_drum_send_gain(track, ramp, g.track_step);   // 8d510ea8
    for (u32 i = 0; i < kDrumSendWords; ++i) buses.main[i] = q31::add(buses.main[i], q31::multiply(g.main, host_sum[i]));
    // The send buses are stock's own memory: the hooks write them in place.
    std::memcpy(host_fx1, buses.fx1, sizeof host_fx1);
    std::memcpy(host_fx2, buses.fx2, sizeof host_fx2);
    // 8d511088: FX I's source; word k at the gain stepped k + 1 times.
    const i32* source = hooks ? drum_send_source(sdk_drum_send_state, 0, track, host_sum) : host_sum;
    i32 gain = g.fx1;
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        gain = q31::add(gain, g.step);
        host_fx1[i] = q31::add(host_fx1[i], q31::multiply(gain, source[i]));
    }
    // 8d5110ca: FX II's source.
    source = hooks ? drum_send_source(sdk_drum_send_state, 1, track, host_sum) : host_sum;
    for (u32 i = 0; i < kDrumSendWords; ++i) host_fx2[i] = q31::add(host_fx2[i], q31::multiply(g.fx2, source[i]));
    // What the dry needs: the send slots' effects, FX II's knob and the dry word.
    static FakeEffect effects[2];
    for (u32 s = 0; s < 2; ++s) {
        effects[s] = FakeEffect{fake_effect_table, s ? g.dry2 : g.dry1, s};
        host_effects[s] = (g.empty >> s & 1) ? nullptr : &effects[s];
    }
    put(host_records + track * 0xe8 + 0x5c, g.k2);
    host_dry_words[track] = g.x;
    DrySmoother& smoother = host_smoothers[hooks ? 1 : 0][track];
    if (hooks) {
        // The engine's frame as the hook reads it: FX I's gain where its loop
        // left it, the dry smoother and the main out ([SP + 0x58], SP = FP - 0xe4).
        alignas(8) static unsigned char frame[0x100];
        unsigned char* fp = frame + 0xf0;
        put(fp - 0x8, gain);
        put(fp - 0x34, &smoother);
        put(fp - 0xe4 + 0x58, static_cast<i32*>(buses.dry));
        sdk_drum_send_own_mix(track, g.level, fp);              // 8d511116
    }
    std::memcpy(buses.fx1, host_fx1, sizeof host_fx1);
    std::memcpy(buses.fx2, host_fx2, sizeof host_fx2);
    // 8d511122..8d51117e: the dry target D x, smoothed, every word of the sum into the main out.
    const i32 target = q31::multiply(stock_dry(g), g.x);
    i32 dry = target, step = 0;
    if (smoother.reset) {
        smoother.reset = 0;
    } else {
        step = drum_send_subtract(target, smoother.current) >> smoother.shift;
        dry = smoother.current;
    }
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        dry = q31::add(dry, step);
        buses.dry[i] = q31::add(buses.dry[i], q31::multiply(dry, host_sum[i]));
    }
    smoother.current = dry;
    smoother.step = step;
}

bool same(const Buses& a, const Buses& b) { return !std::memcmp(&a, &b, sizeof a); }
void fill(i32* words, i32 scale) { for (u32 i = 0; i < kDrumSendWords; ++i) words[i] = scale ? noise() / scale : noise(); }
bool idle() {
    return !sdk_drum_send_state.bus[0].track && !sdk_drum_send_state.bus[1].track &&
           !sdk_drum_send_state.own[0] && !sdk_drum_send_state.own[1];
}
// Q31 multiply as stock's (T) form: the product floored to Q31.
int64_t times(int64_t gain, int64_t value) {
    const int64_t product = gain * value;
    return product >= 0 ? product >> 31 : -((-product + ((int64_t(1) << 31) - 1)) >> 31);
}
// Sums worked out apart from the hooks, for one send: the followers (TRK keys
// and stock voices) and each own-amount key at its amount, each then at its
// frame's track gain, once. No word here comes near saturation.
void expected(i32* followers, i32* owned, const i32* start, const StockVoice* voices, u32 count, u32 send,
              const Gains& g) {
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        int64_t f = start[i], o = 0;
        for (u32 v = 0; v < count; ++v) {
            const u32 code = drum_send_code(voices[v].handle.vtable == kit_table ? voices[v].handle.codes : 0, send);
            if (!code) f += voices[v].audio[i];
            else o += times(kDrumSendGains[send][code], voices[v].audio[i]);
        }
        CHECK(f > -(int64_t(1) << 31) && f < (int64_t(1) << 31) && o > -(int64_t(1) << 31) && o < (int64_t(1) << 31));
        const int64_t gain = track_gain(g, i / 2);
        followers[i] = static_cast<i32>(times(gain, f));
        owned[i] = static_cast<i32>(times(gain, o));
    }
}

// Random voices, gains and bus contents, from quiet to saturating: stock
// voices and a kit's followers mix exactly as stock does.
void stock_and_followers_are_unchanged() {
    static StockVoice voices[8];
    static i32 start[kDrumSendWords];
    for (u32 round = 0; round < 300; ++round) {
        const i32 scale = round % 4 == 0 ? 0 : 1 << (round % 7);   // 0: full-scale, saturating sums
        const u32 count = 1 + round % 8;
        for (u32 v = 0; v < count; ++v) {
            fill(voices[v].audio, scale);
            // Stock voices carrying stray codes, a kit's followers, or both on one track.
            const bool kit = round % 3 != 0 && (v + round) % 2 == 0;
            bind(voices[v], kit, kit ? 0 : pair(round % 8, (round + v) % 8));
        }
        fill(start, round % 5 == 0 ? 1 << 4 : 1 << 30);
        // The track gain from anywhere to anywhere, a quarter of the way per block,
        // or held (the smoother's first block, step 0) every fifth round.
        const i32 from = noise() & 0x7fffffff, to = noise() & 0x7fffffff;
        const Gains g = {noise(), noise() & 0x7fffffff, noise() >> 10, noise() & 0x7fffffff, noise() & 0x7fffffff,
                         round % 5 == 0 ? to : from, round % 5 == 0 ? 0 : (to >> 8) - (from >> 8),
                         noise() & 0x7fffffff, noise(), noise(), noise() & 0x7fffffff, round % 4};
        static Buses stock_buses, hooked;
        fill(stock_buses.main, 4); fill(stock_buses.fx1, 4); fill(stock_buses.fx2, 4);
        hooked = stock_buses;
        const u32 track = round % 8;
        mix(stock_buses, track, voices, count, start, g, false, round % 11 == 0);
        mix(hooked, track, voices, count, start, g, true, round % 11 == 0);
        CHECK(same(stock_buses, hooked));
        CHECK(idle());
    }
}

// With the track's sends at 0, a key at 100 is wet: its send is its voice at
// the track's gain and level, and another key on the same track stays dry.
void own_amounts_ignore_the_track_knob() {
    static StockVoice voices[3];
    static i32 start[kDrumSendWords];
    static Buses buses;
    std::memset(start, 0, sizeof start);
    std::memset(&buses, 0, sizeof buses);
    for (u32 v = 0; v < 3; ++v) fill(voices[v].audio, 1 << 3);
    bind(voices[0], true, pair(7, 7));   // snare: 100 on both
    bind(voices[1], true, pair(1, 1));   // kick: dry on both
    bind(voices[2], true, 0);            // hat: TRK
    const i32 level = 0x60000000;
    const Gains off = {0x50000000, 0, 0, 0, level, kTrackFrom, kTrackStep};
    mix(buses, 3, voices, 3, start, off, true, false);
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        const i32 gain = track_gain(off, i / 2);
        CHECK(buses.fx1[i] == q31::multiply(level, q31::multiply(gain, q31::multiply(kDrumSendGains[0][7], voices[0].audio[i]))));
        CHECK(buses.fx2[i] == q31::multiply(level, q31::multiply(gain, q31::multiply(q31::maximum, voices[0].audio[i]))));
    }
    // The main mix is stock's: every voice, dry or wet.
    for (u32 i = 0; i < kDrumSendWords; ++i)
        CHECK(buses.main[i] == q31::multiply(off.main, q31::multiply(track_gain(off, i / 2),
              q31::add(q31::add(voices[0].audio[i], voices[1].audio[i]), voices[2].audio[i]))));
    // Muted (level 0): from that block on, nothing of the snare reaches the sends.
    const Gains muted = {0, 0, 0, 0, 0, kTrackHeld, 0};
    std::memset(&buses, 0, sizeof buses);
    mix(buses, 3, voices, 3, start, muted, true, false);
    for (u32 i = 0; i < kDrumSendWords; ++i) CHECK(!buses.fx1[i] && !buses.fx2[i]);
    // The same with the track gain at 0 (a scene mute, or engine volume 0) and the level up.
    const Gains silent = {0x50000000, 0, 0, 0, level, 0, 0};
    std::memset(&buses, 0, sizeof buses);
    mix(buses, 3, voices, 3, start, silent, true, false);
    for (u32 i = 0; i < kDrumSendWords; ++i) CHECK(!buses.fx1[i] && !buses.fx2[i]);
}

// Overlapping voices with mixed settings: TRK keys follow the track's send in
// stock's loops, own-amount keys add at the level, one send at a time too.
void mixed_settings_route_each_voice() {
    static StockVoice voices[5];
    static i32 start[kDrumSendWords];
    static Buses buses;
    const Gains g = {0x60000000, 0x10000000, 0x00100000, 0x50000000, 0x40000000, kTrackFrom, kTrackStep};
    // FX I: dry, TRK, 50, 100, TRK; FX II: TRK, 17, dry, TRK, 83.
    const u32 codes[5] = {pair(1, 0), pair(0, 2), pair(4, 1), pair(7, 0), pair(0, 6)};
    for (u32 v = 0; v < 5; ++v) { fill(voices[v].audio, 1 << 4); bind(voices[v], true, codes[v]); }
    fill(start, 1 << 6);
    std::memset(&buses, 0, sizeof buses);
    mix(buses, 2, voices, 5, start, g, true, false);
    static i32 follow1[kDrumSendWords], own1[kDrumSendWords], follow2[kDrumSendWords], own2[kDrumSendWords];
    expected(follow1, own1, start, voices, 5, 0, g);
    expected(follow2, own2, start, voices, 5, 1, g);
    i32 gain = g.fx1;
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        gain = q31::add(gain, g.step);
        CHECK(buses.fx1[i] == q31::add(q31::multiply(gain, follow1[i]), q31::multiply(g.level, own1[i])));
        CHECK(buses.fx2[i] == q31::add(q31::multiply(g.fx2, follow2[i]), q31::multiply(g.level, own2[i])));
    }
    // Own amounts on FX II only: FX I and the main mix are stock's, word for word.
    for (u32 v = 0; v < 5; ++v) bind(voices[v], true, pair(0, 1 + v % 7));
    static Buses stock_buses;
    std::memset(&stock_buses, 0, sizeof stock_buses);
    buses = stock_buses;
    mix(stock_buses, 4, voices, 5, start, g, false, false);
    mix(buses, 4, voices, 5, start, g, true, false);
    CHECK(!std::memcmp(stock_buses.main, buses.main, sizeof buses.main));
    CHECK(!std::memcmp(stock_buses.fx1, buses.fx1, sizeof buses.fx1));
    CHECK(std::memcmp(stock_buses.fx2, buses.fx2, sizeof buses.fx2) != 0);
    // A stock voice on a track with own amounts follows the track.
    bind(voices[1], false, pair(5, 5));
    bind(voices[0], true, pair(7, 1));
    std::memset(start, 0, sizeof start);
    std::memset(&buses, 0, sizeof buses);
    mix(buses, 6, voices, 2, start, g, true, false);
    CHECK(buses.fx2[9] == q31::multiply(g.fx2, q31::multiply(track_gain(g, 4), voices[1].audio[9])));
}

// A key at N sends what the track's knob at N sends, on both sends, at several
// levels and track gains (held near unity, held at the preset default 0.069,
// ramping): one block of a TRK key with the track's knobs at N (stock's gains,
// built as its engine builds them from the level and the knob) against the
// same voice set to N with the knobs at 0. Rounding order differs (the key's
// amount is applied before the track gain and the level): each side floors
// three times, so they agree within 3 LSB. Before the track gain reached the
// own-amount sums, a key at 100 against the knob at 100 was 17-23 dB hot here.
void own_amount_matches_the_knob() {
    static StockVoice voice;
    static i32 start[kDrumSendWords];
    static Buses knob, own;
    std::memset(start, 0, sizeof start);
    const i32 levels[] = {q31::maximum, 0x5a827999, 0x40000000, 0x16a09e66};
    const i32 tracks[3][2] = {{q31::maximum, 0}, {kTrackHeld, 0}, {kTrackFrom, kTrackStep}};
    for (u32 run = 0; run < 3 * 4; ++run)
        for (u32 code = 1; code < kDrumSendCodes; ++code) {
            const u32 t = run / 4;
            fill(voice.audio, 1 << (code % 3));
            const i32 level = levels[run % 4], position = kDrumSendPositions[code];
            // Stock's gains for a knob at N, written out as its engine computes them
            // (8d510d7a..8d510daa, 8d510e0a): FX I level x (0.8 k^2 + 0.2 k), FX II level x k.
            const i32 taper = q31::add(q31::multiply(q31::multiply(position, position), 1717986918),
                                       q31::multiply(position, 429496729));
            const Gains at_n = {0, q31::multiply(level, taper), 0, q31::multiply(level, position), level,
                                tracks[t][0], tracks[t][1]};
            const Gains at_0 = {0, 0, 0, 0, level, tracks[t][0], tracks[t][1]};
            std::memset(&knob, 0, sizeof knob);
            std::memset(&own, 0, sizeof own);
            bind(voice, true, 0);
            mix(knob, 5, &voice, 1, start, at_n, true, false);
            bind(voice, true, pair(code, code));
            mix(own, 5, &voice, 1, start, at_0, true, false);
            for (u32 i = 0; i < kDrumSendWords; ++i) {
                const int64_t d1 = int64_t(knob.fx1[i]) - own.fx1[i], d2 = int64_t(knob.fx2[i]) - own.fx2[i];
                CHECK(d1 >= -3 && d1 <= 3 && d2 >= -3 && d2 <= 3);
            }
        }
}

// A key at 50 against the track's knob at 50 as a project stores it
// (0x40002e00, just above half): the same voice through the same ramping track
// gain, in RMS over the block, within 0.001 dB on both sends (the stored knob
// is 0.0002 dB above the key's exact half on FX I, 0.0001 dB on FX II).
void own_fifty_matches_the_stored_knob() {
    static StockVoice voice;
    static i32 start[kDrumSendWords];
    static Buses knob, own;
    std::memset(start, 0, sizeof start);
    fill(voice.audio, 1 << 2);
    const i32 level = 0x5a827999, position = 0x40002e00;
    const i32 taper = q31::add(q31::multiply(q31::multiply(position, position), 1717986918),
                               q31::multiply(position, 429496729));
    const Gains at_50 = {0, q31::multiply(level, taper), 0, q31::multiply(level, position), level, kTrackFrom, kTrackStep};
    const Gains at_0 = {0, 0, 0, 0, level, kTrackFrom, kTrackStep};
    std::memset(&knob, 0, sizeof knob);
    std::memset(&own, 0, sizeof own);
    bind(voice, true, 0);
    mix(knob, 2, &voice, 1, start, at_50, true, false);
    bind(voice, true, pair(4, 4));
    mix(own, 2, &voice, 1, start, at_0, true, false);
    for (u32 send = 0; send < kDrumSends; ++send) {
        const i32* a = send ? knob.fx2 : knob.fx1;
        const i32* b = send ? own.fx2 : own.fx1;
        double ea = 0, eb = 0;
        for (u32 i = 0; i < kDrumSendWords; ++i) { ea += double(a[i]) * a[i]; eb += double(b[i]) * b[i]; }
        CHECK(eb > 0);
        const double db = 10.0 * std::log10(ea / eb);
        CHECK(db > -0.001 && db < 0.001);
    }
}

// On a track with a key of its own, the TRK keys' send is stock's send of
// those keys alone, word for word, at every kind of track gain: the followers'
// sum takes the track gain exactly once. The own key is dry here (its amount
// 0), so the bus holds the followers alone.
void followers_send_as_stock_beside_an_own_key() {
    static StockVoice voices[4], followers[3];
    static i32 start[kDrumSendWords];
    static Buses hooked, stock_buses;
    const i32 tracks[4][2] = {{kTrackHeld, 0}, {kTrackFrom, kTrackStep}, {0x08000000, (0x60000000 >> 8) - (0x08000000 >> 8)},
                              {q31::maximum, 0}};
    for (u32 t = 0; t < 4; ++t) {
        fill(start, 1 << 6);
        for (u32 v = 0; v < 4; ++v) fill(voices[v].audio, 1 << 4);
        bind(voices[0], true, 0);            // TRK, before the own key: in stock's sum so far
        bind(voices[1], true, pair(1, 1));   // the own key, dry on both
        bind(voices[2], true, 0);            // TRK
        bind(voices[3], false, pair(7, 7));  // a stock voice: follows whatever its word holds
        followers[0] = voices[0];
        followers[1] = voices[2];
        followers[2] = voices[3];
        for (u32 v = 0; v < 3; ++v) bind(followers[v], v < 2, v < 2 ? 0 : pair(7, 7));
        const Gains g = {0x60000000, 0x10000000, 0x00100000, 0x50000000, 0x40000000, tracks[t][0], tracks[t][1]};
        std::memset(&hooked, 0, sizeof hooked);
        std::memset(&stock_buses, 0, sizeof stock_buses);
        mix(hooked, 5, voices, 4, start, g, true, false);
        mix(stock_buses, 5, followers, 3, start, g, false, false);
        CHECK(!std::memcmp(hooked.fx1, stock_buses.fx1, sizeof hooked.fx1));
        CHECK(!std::memcmp(hooked.fx2, stock_buses.fx2, sizeof hooked.fx2));
        // The main mix has every voice, the own key's too.
        CHECK(std::memcmp(hooked.main, stock_buses.main, sizeof hooked.main) != 0);
    }
}

// Each block's own-amount sums go in at that block's level, every frame alike,
// as stock's send loops hold their gains for the block (FX I's step is 0 on the
// device, 8d510d74): a level change takes effect at the block it arrives in.
// The track gain is held here, as it is once its smoother settles.
void level_is_each_blocks() {
    static StockVoice voice;
    static i32 start[kDrumSendWords];
    static Buses buses;
    std::memset(start, 0, sizeof start);
    for (u32 i = 0; i < kDrumSendWords; ++i) voice.audio[i] = 1 << 28;
    bind(voice, true, pair(7, 7));
    const i32 own1 = q31::multiply(kDrumSendGains[0][7], 1 << 28), own2 = q31::multiply(kDrumSendGains[1][7], 1 << 28);
    for (i32 level : {0x10000000, 0x50000000, 0, 0x7fffffff}) {
        const Gains g = {0, 0, 0, 0, level, kTrackHeld, 0};
        std::memset(&buses, 0, sizeof buses);
        mix(buses, 0, &voice, 1, start, g, true, false);
        for (u32 i = 0; i < kDrumSendWords; ++i)
            CHECK(buses.fx1[i] == q31::multiply(level, q31::multiply(kTrackHeld, own1)) &&
                  buses.fx2[i] == q31::multiply(level, q31::multiply(kTrackHeld, own2)));
    }
}

// The sums belong to one track of one block: the next track, or the same
// track after a block stock skipped, mixes as stock does again.
void state_ends_with_its_track() {
    static StockVoice voices[2];
    static i32 start[kDrumSendWords];
    std::memset(start, 0, sizeof start);
    fill(voices[0].audio, 1 << 4); bind(voices[0], true, pair(3, 6));
    fill(voices[1].audio, 1 << 4); bind(voices[1], false, 0);
    const Gains g = {0x40000000, 0x20000000, 0, 0x30000000, 0x40000000, kTrackFrom, kTrackStep};
    static Buses stock_buses, hooked;
    std::memset(&stock_buses, 0, sizeof stock_buses);
    std::memset(&hooked, 0, sizeof hooked);
    mix(hooked, 1, voices, 1, start, g, true, true);       // a kit track stock skipped
    CHECK(sdk_drum_send_state.bus[0].track == 2 && sdk_drum_send_state.own[1] == 2);
    mix(stock_buses, 1, voices + 1, 1, start, g, false, false);
    mix(hooked, 1, voices + 1, 1, start, g, true, false);  // the same track, next block, a stock voice
    CHECK(same(stock_buses, hooked));
    // A stale state from another track never redirects this one's sends.
    sdk_drum_send_state.bus[1].track = 4;
    sdk_drum_send_state.bus[1].source = nullptr;
    CHECK(drum_send_source(sdk_drum_send_state, 1, 2, host_sum) == host_sum);
    CHECK(drum_send_source(sdk_drum_send_state, 0, 3, host_sum) == host_sum);
    CHECK(drum_send_source(sdk_drum_send_state, 1, 3, host_sum) == nullptr);
}

void codes_and_amounts() {
    // Payload word 6 carries FX I's code in bits 25..27 and FX II's in 28..30,
    // only on a key the kit set up; FILTER and SRR below them stay as they are.
    const i32 marked = i32(42u * 32537631u + 16268815u);
    i32 payload[8] = {marked, 0, 0, 0, 0, 0, 0, 0};
    for (u32 fx1 = 0; fx1 < kDrumSendCodes; ++fx1)
        for (u32 fx2 = 0; fx2 < kDrumSendCodes; ++fx2) {
            const i32 fields = drum_send_store(drum_send_store(0x01ab0000, 0, fx1), 1, fx2);
            payload[6] = fields | i32(kDrumPadReady);
            CHECK(drum_send_codes(payload) == pair(fx1, fx2));
            CHECK((payload[6] & 0x01ffffff) == (0x01ab0000 | i32(kDrumPadReady)));
            CHECK((static_cast<u32>(payload[6]) >> 25 & 7) == fx1 && (static_cast<u32>(payload[6]) >> 28 & 7) == fx2);
            // Without the set-up value (an SDK 1.9 kit's key, or a preset's) it follows the track.
            payload[6] = fields;
            CHECK(drum_send_codes(payload) == 0);
            payload[6] = fields | 0x1234;
            CHECK(drum_send_codes(payload) == 0);
        }
    payload[6] = 0x7e000000 | i32(kDrumPadReady);
    CHECK(drum_send_codes(payload) == pair(7, 7));
    payload[0] = 0;
    CHECK(drum_send_codes(payload) == 0);
    CHECK(drum_send_codes(nullptr) == 0);
    // Amounts: TRK has none of its own, then knob positions 0, 17, 33, 50, 67,
    // 83, 100, through each send's stock curve.
    const i32 shown[kDrumSendCodes] = {0, 0, 17, 33, 50, 67, 83, 100};
    for (u32 code = 0; code < kDrumSendCodes; ++code) {
        CHECK(drum_send_percent(code) == shown[code]);
        const double position = double(kDrumSendPositions[code]) / 2147483648.0;
        const double want = code ? (code - 1) / 6.0 : 0.0;
        CHECK(position > want - 1e-9 && position < want + 1e-9);
        for (u32 send = 0; send < kDrumSends; ++send)
            CHECK(drum_send_gain(send, code) == (code ? drum_send_curve(send, kDrumSendPositions[code]) : 0));
    }
    // FX I's curve is stock's 0.8 k^2 + 0.2 k: 30 percent of the level at 50.
    CHECK(drum_send_gain(0, 4) > 644245000 && drum_send_gain(0, 4) < 644245200);
}
}

// Stock Drumline voices (a drumline_page extension): each sends the codes
// its render kept (sdk_drum_send_dbox), mixed exactly as a padSend kit's key
// with those codes; codes of 0, as every build without one keeps them,
// mix as stock does; an object outside stock's 27 follows the track.
void drumline_voices_send_their_kept_codes() {
    static StockVoice lines[4], kits[4];
    static i32 start[kDrumSendWords];
    static Buses a, b;
    fill(start, 1 << 6);
    const Gains g = {0x60000000, 0x10000000, 0x00100000, 0x50000000, 0x40000000, kTrackFrom, kTrackStep};
    const u32 codes[4] = {pair(7, 1), pair(0, 4), pair(2, 0), 0};
    for (u32 v = 0; v < 4; ++v) {
        fill(lines[v].audio, 1 << 4);
        std::memcpy(kits[v].audio, lines[v].audio, sizeof lines[v].audio);
        sdk_drum_send_dbox[3 + v] = static_cast<opxy::u8>(codes[v]);
        bind_dbox(lines[v], 3 + v);
        bind(kits[v], true, codes[v]);
    }
    std::memset(&a, 0, sizeof a);
    std::memset(&b, 0, sizeof b);
    sdk_drum_send_dbox[0] = static_cast<opxy::u8>(pair(7, 7));
    mix(a, 1, lines, 4, start, g, true, false);
    mix(b, 1, kits, 4, start, g, true, false);
    CHECK(same(a, b));
    CHECK(std::memcmp(a.fx1, a.fx2, sizeof a.fx1) != 0);
    // Without one every kept code stays 0: the hooks mix as stock.
    for (u32 v = 0; v < kDboxVoices; ++v) sdk_drum_send_dbox[v] = 0;
    static Buses stock_buses;
    std::memset(&a, 0, sizeof a);
    std::memset(&stock_buses, 0, sizeof stock_buses);
    mix(stock_buses, 1, lines, 4, start, g, false, false);
    mix(a, 1, lines, 4, start, g, true, false);
    CHECK(same(stock_buses, a) && idle());
    // An index outside the 27 reads as TRK.
    sdk_drum_send_dbox[0] = static_cast<opxy::u8>(pair(7, 7));
    bind_dbox(lines[0], kDboxVoices);
    std::memset(&a, 0, sizeof a);
    mix(a, 1, lines, 1, start, g, true, false);
    std::memset(&stock_buses, 0, sizeof stock_buses);
    mix(stock_buses, 1, lines, 1, start, g, false, false);
    CHECK(same(stock_buses, a));
    sdk_drum_send_dbox[0] = 0;
}

// A key's own amounts give it the dry stock gives a track whose knobs sit at
// those amounts (D = L - G1 (1 - d1) - G2 (1 - d2), both sends together, D
// below 0 included), while its TRK sends keep the track's knob: one key at
// codes (c1, c2) on a track with knobs (k1, k2), against the same voice as TRK
// on a track whose knobs sit at (c1 ? position(c1) : k1, c2 ? ... : k2), for
// every pair of codes, several knob settings, effect dry levels and empty
// slots. Smoothers held (each run starts them on their reset). Every side
// floors a few times: dry, FX I and FX II agree within 6 LSB a word.
void own_amounts_take_their_own_dry() {
    static StockVoice voice;
    static i32 start[kDrumSendWords];
    static Buses knob, own;
    std::memset(start, 0, sizeof start);
    const i32 level = 0x5a827999, x = 0x7fffffff;
    const i32 knobs[4][2] = {{0, 0}, {q31::maximum, 0}, {q31::maximum, q31::maximum}, {0x40002e00, 0x2aaaaaab}};
    const i32 drys[3] = {0, 0x40000000, q31::maximum};
    for (u32 c1 = 0; c1 < kDrumSendCodes; ++c1)
        for (u32 c2 = 0; c2 < kDrumSendCodes; ++c2)
            for (u32 k = 0; k < 4; ++k)
                for (u32 d = 0; d < 3; ++d)
                    for (u32 empty = 0; empty < 4; empty += 3) {
                        if (!c1 && !c2) continue;   // a TRK key: stock's path, tested elsewhere
                        fill(voice.audio, 1 << (1 + (c1 + c2 + k) % 3));
                        const i32 p1 = c1 ? kDrumSendPositions[c1] : knobs[k][0];
                        const i32 p2 = c2 ? kDrumSendPositions[c2] : knobs[k][1];
                        const Gains at_knobs = {0, q31::multiply(level, drum_send_curve(0, knobs[k][0])), 0,
                                                q31::multiply(level, knobs[k][1]), level, kTrackHeld, 0,
                                                knobs[k][1], drys[d], drys[(d + 1) % 3], x, empty};
                        const Gains at_amounts = {0, q31::multiply(level, drum_send_curve(0, p1)), 0,
                                                  q31::multiply(level, p2), level, kTrackHeld, 0,
                                                  p2, drys[d], drys[(d + 1) % 3], x, empty};
                        reset_smoothers();
                        std::memset(&own, 0, sizeof own);
                        std::memset(&knob, 0, sizeof knob);
                        bind(voice, true, pair(c1, c2));
                        mix(own, 6, &voice, 1, start, at_knobs, true, false);
                        bind(voice, true, 0);
                        mix(knob, 7, &voice, 1, start, at_amounts, true, false);
                        for (u32 i = 0; i < kDrumSendWords; ++i) {
                            const int64_t a = int64_t(own.dry[i]) - knob.dry[i];
                            const int64_t b = int64_t(own.fx1[i]) - knob.fx1[i];
                            const int64_t c = int64_t(own.fx2[i]) - knob.fx2[i];
                            CHECK(a >= -6 && a <= 6 && b >= -6 && b <= 6 && c >= -6 && c <= 6);
                        }
                    }
    // The report: the track's FX I at 100 into an effect that keeps no dry, so
    // the track's dry is 0; a key at 0 on FX I (TRK on FX II, at 0) plays dry at
    // the level, not silent, and a TRK key beside it stays silent as on stock.
    static StockVoice keys[2];
    fill(keys[0].audio, 1 << 3);
    std::memcpy(keys[1].audio, keys[0].audio, sizeof keys[0].audio);
    bind(keys[0], true, pair(1, 0));
    bind(keys[1], true, 0);
    const Gains full = {0, level, 0, 0, level, kTrackHeld, 0, 0, 0, 0, x, 0};
    CHECK(stock_dry(full) >= 0 && stock_dry(full) <= 1);   // L less L at full, floored
    reset_smoothers();
    std::memset(&own, 0, sizeof own);
    std::memset(&knob, 0, sizeof knob);
    mix(own, 2, keys, 1, start, full, true, false);
    mix(knob, 3, keys + 1, 1, start, full, true, false);
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        CHECK(knob.dry[i] == 0);
        const int64_t want = q31::multiply(q31::multiply(level, x), q31::multiply(kTrackHeld, keys[0].audio[i]));
        CHECK(own.dry[i] - want >= -4 && own.dry[i] - want <= 4);
    }
    // Both sends at 100 into effects that keep no dry: stock's TRK dry is the
    // level inverted; a key at 0 on both plays it upright.
    const Gains both = {0, level, 0, level, level, kTrackHeld, 0, q31::maximum, 0, 0, x, 0};
    CHECK(stock_dry(both) < 0);
    bind(keys[0], true, pair(1, 1));
    reset_smoothers();
    std::memset(&own, 0, sizeof own);
    std::memset(&knob, 0, sizeof knob);
    mix(own, 2, keys, 1, start, both, true, false);
    mix(knob, 3, keys + 1, 1, start, both, true, false);
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        const int64_t sum = int64_t(own.dry[i]) + knob.dry[i];
        CHECK(sum >= -6 && sum <= 6);
    }
}

// While the track's knob moves, stock glides its dry (the smoother, a 256th of
// the gap a word) and the key's correction glides by the same rule: a key at 0
// under a knob going from 0 to 100 into an effect that keeps no dry keeps the
// level, block after block, as a key on a track whose knob stays at 0, within
// the two smoothers' flooring (a few hundred LSB of gain, under -120 dB); a TRK
// key glides down as stock's. The key's dry also starts right on a track whose
// smoother has settled without it (its gains start at their targets).
void own_dry_glides_with_stocks() {
    static StockVoice key, trk;
    static i32 start[kDrumSendWords];
    static Buses own, held, follower;
    std::memset(start, 0, sizeof start);
    fill(key.audio, 1 << 3);
    std::memcpy(trk.audio, key.audio, sizeof key.audio);
    bind(key, true, pair(1, 0));
    bind(trk, true, 0);
    const i32 level = 0x60000000, x = 0x7fffffff;
    const Gains at_0 = {0, 0, 0, 0, level, kTrackHeld, 0, 0, 0, 0, x, 0};
    const Gains at_100 = {0, level, 0, 0, level, kTrackHeld, 0, 0, 0, 0, x, 0};
    reset_smoothers();
    // Settle tracks 1..3 at the knob's 0 with no own key, then the key arrives on track 1.
    for (u32 block = 0; block < 4; ++block) {
        std::memset(&own, 0, sizeof own);
        mix(own, 1, &trk, 1, start, at_0, true, false);
        mix(own, 3, &trk, 1, start, at_0, true, false);
    }
    for (u32 block = 0; block < 40; ++block) {
        std::memset(&own, 0, sizeof own);
        std::memset(&held, 0, sizeof held);
        std::memset(&follower, 0, sizeof follower);
        mix(own, 1, &key, 1, start, block ? at_100 : at_0, true, false);
        mix(held, 2, &trk, 1, start, at_0, true, false);
        mix(follower, 3, &trk, 1, start, block ? at_100 : at_0, true, false);
        for (u32 i = 0; i < kDrumSendWords; ++i) {
            const int64_t d = int64_t(own.dry[i]) - held.dry[i];
            CHECK(d >= -256 && d <= 256);
        }
        if (block == 1) {
            // The TRK key glided, not jumped: by the block's last word stock's
            // dry gain has come about half way down.
            u32 at = kDrumSendWords - 8;   // the loudest of the block's last words
            for (u32 i = at; i < kDrumSendWords; ++i)
                if (std::abs(held.dry[i] / 2) > std::abs(held.dry[at] / 2)) at = i;
            const int64_t f = follower.dry[at], h = held.dry[at];
            CHECK(h != 0 && (f > 0) == (h > 0));
            CHECK(4 * (f < 0 ? -f : f) > (h < 0 ? -h : h) && 4 * (f < 0 ? -f : f) < 3 * (h < 0 ? -h : h));
        }
    }
}

int main() {
    reset_smoothers();
    drumline_voices_send_their_kept_codes();
    codes_and_amounts();
    stock_and_followers_are_unchanged();
    own_amounts_ignore_the_track_knob();
    mixed_settings_route_each_voice();
    own_amount_matches_the_knob();
    own_fifty_matches_the_stored_knob();
    followers_send_as_stock_beside_an_own_key();
    level_is_each_blocks();
    state_ends_with_its_track();
    own_amounts_take_their_own_dry();
    own_dry_glides_with_stocks();
    std::puts("drum sends ok");
    return 0;
}
