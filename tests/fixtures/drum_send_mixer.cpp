// Per-key sends: drum_send.cpp's hooks inside a host model of stock 1.2.1's
// mixer (8d510760), as drum_send_hooks.s places them. The model follows the
// stock instructions: the voice loop's saturating add of each rendered voice
// into the track's 128-word stereo sum (8d51c7b0..8d51c7d8), then the main mix
// at a constant gain (8d511038..8d511084), FX I at a gain stepped every word
// (8d511088..8d5110c8) and FX II at a constant gain (8d5110ca..8d51110e), then
// the own-amount hook (8d511116). A skipped track (8d51237a) mixes nothing.
// The test substitutes host buffers for stock's 0x11903600, 0x11b01548 and the
// send buses 0x84caa140 and 0x84caa340.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <opxy/q31.hpp>
// The stock buffers drum_send.cpp names, as the test substitutes them.
extern opxy::i32 host_sum[], host_voice[], host_fx1[], host_fx2[];
#include "drum_send.cpp"

using opxy::i32;
using opxy::u32;
namespace q31 = opxy::q31;

i32 host_sum[kDrumSendWords], host_voice[kDrumSendWords], host_fx1[kDrumSendWords], host_fx2[kDrumSendWords];

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

// The engine's gains for one track: main, FX I's start and step, FX II's, and
// the level ([FP-0x68]) both sends are scaled from.
struct Gains { i32 main, fx1, step, fx2, level; };
struct Buses { i32 main[kDrumSendWords], fx1[kDrumSendWords], fx2[kDrumSendWords]; };

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
    if (hooks) sdk_drum_send_own_mix(track, g.level);          // 8d511116
    std::memcpy(buses.fx1, host_fx1, sizeof host_fx1);
    std::memcpy(buses.fx2, host_fx2, sizeof host_fx2);
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
// and stock voices) and each own-amount key at its amount. No word here comes
// near saturation.
void expected(i32* followers, i32* owned, const i32* start, const StockVoice* voices, u32 count, u32 send) {
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        int64_t f = start[i], o = 0;
        for (u32 v = 0; v < count; ++v) {
            const u32 code = drum_send_code(voices[v].handle.vtable == kit_table ? voices[v].handle.codes : 0, send);
            if (!code) f += voices[v].audio[i];
            else o += times(kDrumSendGains[send][code], voices[v].audio[i]);
        }
        CHECK(f > -(int64_t(1) << 31) && f < (int64_t(1) << 31) && o > -(int64_t(1) << 31) && o < (int64_t(1) << 31));
        followers[i] = static_cast<i32>(f);
        owned[i] = static_cast<i32>(o);
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
        const Gains g = {noise(), noise() & 0x7fffffff, noise() >> 10, noise() & 0x7fffffff, noise() & 0x7fffffff};
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
// the track's level, and another key on the same track stays dry.
void own_amounts_ignore_the_track_knob() {
    static StockVoice voices[3];
    static i32 start[kDrumSendWords];
    static Buses buses;
    std::memset(start, 0, sizeof start);
    std::memset(&buses, 0, sizeof buses);
    sdk_drum_send_state.level_known = 0;
    for (u32 v = 0; v < 3; ++v) fill(voices[v].audio, 1 << 3);
    bind(voices[0], true, pair(7, 7));   // snare: 100 on both
    bind(voices[1], true, pair(1, 1));   // kick: dry on both
    bind(voices[2], true, 0);            // hat: TRK
    const i32 level = 0x60000000;
    const Gains off = {0x50000000, 0, 0, 0, level};
    mix(buses, 3, voices, 3, start, off, true, false);
    for (u32 i = 0; i < kDrumSendWords; ++i) {
        CHECK(buses.fx1[i] == q31::multiply(level, q31::multiply(kDrumSendGains[0][7], voices[0].audio[i])));
        CHECK(buses.fx2[i] == q31::multiply(level, q31::multiply(q31::maximum, voices[0].audio[i])));
    }
    // The main mix is stock's: every voice, dry or wet.
    for (u32 i = 0; i < kDrumSendWords; ++i)
        CHECK(buses.main[i] == q31::multiply(off.main,
              q31::add(q31::add(voices[0].audio[i], voices[1].audio[i]), voices[2].audio[i])));
    // Muted (level 0): after one block ramping down, nothing of the snare reaches the sends.
    const Gains muted = {0, 0, 0, 0, 0};
    mix(buses, 3, voices, 3, start, muted, true, false);
    std::memset(&buses, 0, sizeof buses);
    mix(buses, 3, voices, 3, start, muted, true, false);
    for (u32 i = 0; i < kDrumSendWords; ++i) CHECK(!buses.fx1[i] && !buses.fx2[i]);
}

// Overlapping voices with mixed settings: TRK keys follow the track's send in
// stock's loops, own-amount keys add at the level, one send at a time too.
void mixed_settings_route_each_voice() {
    static StockVoice voices[5];
    static i32 start[kDrumSendWords];
    static Buses buses;
    const Gains g = {0x60000000, 0x10000000, 0x00100000, 0x50000000, 0x40000000};
    // FX I: dry, TRK, 50, 100, TRK; FX II: TRK, 17, dry, TRK, 83.
    const u32 codes[5] = {pair(1, 0), pair(0, 2), pair(4, 1), pair(7, 0), pair(0, 6)};
    for (u32 v = 0; v < 5; ++v) { fill(voices[v].audio, 1 << 4); bind(voices[v], true, codes[v]); }
    fill(start, 1 << 6);
    std::memset(&buses, 0, sizeof buses);
    sdk_drum_send_state.level_known = 0;
    mix(buses, 2, voices, 5, start, g, true, false);
    static i32 follow1[kDrumSendWords], own1[kDrumSendWords], follow2[kDrumSendWords], own2[kDrumSendWords];
    expected(follow1, own1, start, voices, 5, 0);
    expected(follow2, own2, start, voices, 5, 1);
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
    CHECK(buses.fx2[9] == q31::multiply(g.fx2, voices[1].audio[9]));
}

// A key at N sends what the track's knob at N sends, on both sends and at
// several levels: one block of a TRK key with the track's knobs at N (stock's
// gains, built as its engine builds them from the level and the knob) against
// the same voice set to N with the knobs at 0. Rounding order differs (the
// key's amount is applied before the level), so they agree within 2 LSB.
void own_amount_matches_the_knob() {
    static StockVoice voice;
    static i32 start[kDrumSendWords];
    static Buses knob, own;
    std::memset(start, 0, sizeof start);
    const i32 levels[] = {q31::maximum, 0x5a827999, 0x40000000, 0x16a09e66};
    for (u32 l = 0; l < 4; ++l)
        for (u32 code = 1; code < kDrumSendCodes; ++code) {
            fill(voice.audio, 1 << (code % 3));
            const i32 level = levels[l], position = kDrumSendPositions[code];
            // Stock's gains for a knob at N, written out as its engine computes them
            // (8d510d7a..8d510daa, 8d510e0a): FX I level x (0.8 k^2 + 0.2 k), FX II level x k.
            const i32 taper = q31::add(q31::multiply(q31::multiply(position, position), 1717986918),
                                       q31::multiply(position, 429496729));
            const Gains at_n = {0, q31::multiply(level, taper), 0, q31::multiply(level, position), level};
            const Gains at_0 = {0, 0, 0, 0, level};
            sdk_drum_send_state.level_known = 0;
            std::memset(&knob, 0, sizeof knob);
            std::memset(&own, 0, sizeof own);
            bind(voice, true, 0);
            mix(knob, 5, &voice, 1, start, at_n, true, false);
            bind(voice, true, pair(code, code));
            mix(own, 5, &voice, 1, start, at_0, true, false);
            for (u32 i = 0; i < kDrumSendWords; ++i) {
                const int64_t d1 = int64_t(knob.fx1[i]) - own.fx1[i], d2 = int64_t(knob.fx2[i]) - own.fx2[i];
                CHECK(d1 >= -2 && d1 <= 2 && d2 >= -2 && d2 <= 2);
            }
        }
}

// The level ramps from one block's to the next, as stock steps FX I's gain.
void level_ramps() {
    static StockVoice voice;
    static i32 start[kDrumSendWords];
    static Buses buses;
    std::memset(start, 0, sizeof start);
    for (u32 i = 0; i < kDrumSendWords; ++i) voice.audio[i] = 1 << 28;
    bind(voice, true, pair(7, 0));
    sdk_drum_send_state.level_known = 0;
    const Gains low = {0, 0, 0, 0, 0x10000000}, high = {0, 0, 0, 0, 0x50000000};
    std::memset(&buses, 0, sizeof buses);
    mix(buses, 0, &voice, 1, start, low, true, false);
    std::memset(&buses, 0, sizeof buses);
    mix(buses, 0, &voice, 1, start, high, true, false);
    for (u32 i = 1; i < kDrumSendWords; ++i) CHECK(buses.fx1[i] > buses.fx1[i - 1]);
    CHECK(buses.fx1[0] > q31::multiply(low.level, 1 << 28));
    CHECK(buses.fx1[kDrumSendWords - 1] <= q31::multiply(high.level, 1 << 28));
    CHECK(buses.fx1[kDrumSendWords - 1] > q31::multiply(high.level - (1 << 20), 1 << 28));
}

// The sums belong to one track of one block: the next track, or the same
// track after a block stock skipped, mixes as stock does again.
void state_ends_with_its_track() {
    static StockVoice voices[2];
    static i32 start[kDrumSendWords];
    std::memset(start, 0, sizeof start);
    fill(voices[0].audio, 1 << 4); bind(voices[0], true, pair(3, 6));
    fill(voices[1].audio, 1 << 4); bind(voices[1], false, 0);
    const Gains g = {0x40000000, 0x20000000, 0, 0x30000000, 0x40000000};
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
    // only on a marked pad; FILTER and SRR below them stay as they are.
    const i32 marked = i32(42u * 32537631u + 16268815u);
    i32 payload[8] = {marked, 0, 0, 0, 0, 0, 0, 0};
    for (u32 fx1 = 0; fx1 < kDrumSendCodes; ++fx1)
        for (u32 fx2 = 0; fx2 < kDrumSendCodes; ++fx2) {
            payload[6] = drum_send_store(drum_send_store(0x01ab0000, 0, fx1), 1, fx2);
            CHECK(drum_send_codes(payload) == pair(fx1, fx2));
            CHECK((payload[6] & 0x01ffffff) == 0x01ab0000);
            CHECK((static_cast<u32>(payload[6]) >> 25 & 7) == fx1 && (static_cast<u32>(payload[6]) >> 28 & 7) == fx2);
        }
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

int main() {
    codes_and_amounts();
    stock_and_followers_are_unchanged();
    own_amounts_ignore_the_track_knob();
    mixed_settings_route_each_voice();
    own_amount_matches_the_knob();
    level_ramps();
    state_ends_with_its_track();
    std::puts("drum sends ok");
    return 0;
}
