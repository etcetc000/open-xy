// The shared track call handler (sdk/platform/opxy-1.2.1/track_call.s) and the track
// stage runtime (track_stage.cpp) on the BF707 Functional-Sim, against a direct call
// of the voice loop as the engine makes it at 8d510bea. The loop is a stub with stock's
// frame and argument-area stores (harness.s stub_loop); the engine words it reads are
// stock's own address (0x11b01d48). Freestanding.
#include "track_stage.hpp"
using opxy::u32; using opxy::i32;
namespace stage = opxy::platform::track_stage;

static u32 seed;
static u32 rnd() { seed = seed * 1664525u + 1013904223u; return seed ^ (seed >> 15); }
#define V(addr) (*(volatile u32*)(addr))

struct Call { u32 manager, track, sum, in[8], r0, out[8]; i32 sp; u32 cycles; };
extern "C" {
void run_call(Call* call, u32 which) __asm__("run_call");
u32 stub_render(u32 track, i32* sum) __asm__("stub_render");
void sdk_mixer_begin() __asm__("sdk_mixer_begin");
extern const stage::Stage* sdk_track_stages __asm__("sdk_track_stages");
extern u32 sdk_track_stage_count __asm__("sdk_track_stage_count");
}

// What the stub loop renders on each track this block: so many voices, each a pattern.
static u32 voices[8], begins, resets, processes, seen_voices;
u32 stub_render(u32 track, i32* sum) {
    for (u32 v = 0; v < voices[track]; ++v)
        for (u32 i = 0; i < 128; ++i) sum[i] += (i32)((track << 20) + (v << 12) + i * 3) - 0x40000;
    return voices[track];
}
void sdk_mixer_begin() { ++begins; }

// Engine 40's stage halves the left channel; with no voice it leaves a tail word.
static int ready() { return 1; }
static void reset(u32) { ++resets; }
static void process(u32, i32* sum, const i32*, u32 count) {
    ++processes;
    seen_voices = count;
    for (u32 i = 0; i < 128; i += 2) sum[i] >>= 1;
    if (!count) sum[1] = 100;
}
static const stage::Stage sdk_track_stages_rows[] = {{(const char*)40, ready, reset, process, 3}};
const stage::Stage* sdk_track_stages = sdk_track_stages_rows;
u32 sdk_track_stage_count = 1;

static const u32 ENGINES = 0x11b01d48, SUM = 0x11903600;
static i32 direct[128];

static void prepare(Call& c, u32 track) {
    c.manager = 0x84eab4d8u + rnd() % 64; c.track = track; c.sum = SUM;
    for (u32 k = 0; k < 8; ++k) c.in[k] = rnd() | 1;
    for (u32 i = 0; i < 128; ++i) ((i32*)SUM)[i] = 0;   // the engine clears it before the call
}
static bool same_call(const Call& a, const Call& b) {
    if (a.r0 != b.r0 || a.sp != b.sp || a.sp != 0) return false;
    for (u32 k = 0; k < 8; ++k) if (a.out[k] != a.in[k] || b.out[k] != b.in[k]) return false;
    return true;
}

extern "C" unsigned sim_probe(unsigned* out) {
    u32 w = 0;
    // Tracks whose engine has no stage: the handler must give stock's count, sum and
    // registers. 0 runs the loop as the engine does, 1 through the handler.
    u32 bad = 0, direct_cycles = 0, handler_cycles = 0, n = 0;
    seed = 0x5eed;
    for (u32 t = 0; t < 8; ++t) V(ENGINES + 4 * t) = rnd() % 40;
    for (; n < 256; ++n) {
        const u32 track = n % 8;
        if (n % 37 == 5) V(ENGINES + 4 * track) = rnd() % 40;   // engine changes, none to a stage
        voices[track] = rnd() % 5;
        Call a, b;
        prepare(a, track);
        b = a;
        run_call(&a, 0);
        for (u32 i = 0; i < 128; ++i) direct[i] = ((i32*)SUM)[i];
        for (u32 i = 0; i < 128; ++i) ((i32*)SUM)[i] = 0;
        const u32 before = begins;
        run_call(&b, 1);
        bool ok = same_call(a, b) && begins == before + 1 && a.r0 == voices[track];
        for (u32 i = 0; i < 128; ++i) ok = ok && ((i32*)SUM)[i] == direct[i];
        if (!ok) { ++bad; if (w < 32) out[w++] = 0xbad00000u | n; }
        direct_cycles += a.cycles; handler_cycles += b.cycles;
    }
    out[w++] = 0xaaaa0001; out[w++] = n; out[w++] = bad; out[w++] = direct_cycles / n; out[w++] = handler_cycles / n;
    out[w++] = resets; out[w++] = processes;
    // Engine 40 on track 3: reset once, the stage after the loop, the count kept; with no
    // voice and the stage's tail word, the count the engine gets is 1.
    V(ENGINES + 12) = 40;
    u32 stage_bad = 0;
    for (u32 k = 0; k < 4; ++k) {
        voices[3] = k < 2 ? 2 : 0;
        Call a, b;
        prepare(a, 3);
        b = a;
        run_call(&a, 0);
        for (u32 i = 0; i < 128; ++i) direct[i] = ((i32*)SUM)[i];
        for (u32 i = 0; i < 128; i += 2) direct[i] >>= 1;
        if (!voices[3]) direct[1] = 100;
        for (u32 i = 0; i < 128; ++i) ((i32*)SUM)[i] = 0;
        run_call(&b, 1);
        bool ok = b.r0 == (voices[3] ? 2u : 1u) && b.sp == 0 && seen_voices == voices[3];
        for (u32 j = 0; j < 8; ++j) ok = ok && b.out[j] == b.in[j];
        for (u32 i = 0; i < 128; ++i) ok = ok && ((i32*)SUM)[i] == direct[i];
        if (!ok) ++stage_bad;
        out[w++] = b.cycles;
    }
    out[w++] = 0xaaaa0002; out[w++] = stage_bad; out[w++] = resets; out[w++] = processes;
    return w;
}
