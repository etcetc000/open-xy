// pitch_constants against stock's own code on the BF707 Functional-Sim, stock OS 1.2.1 loaded at its own
// addresses (tests/test_pitch_constants.py). EDIT1 and EDIT2 are the profile's two edits, STOCK1 and STOCK2 the
// stock bytes it says they replace, six halfwords each.
// Output, in order:
//   0xf2000001 w1 w2 w1 w2           stock's float-to-fixed 8d5bf5ec on 1200.0f and 100.0f, twice each
//   0xf2000002 calls mismatches distinct stock_cycles edited_cycles     (booted tables)
//   0xf2000003 calls mismatches distinct stock_cycles edited_cycles     (random pitch tables)
//   0xf2000004 restored stock   the sites hold stock's bytes again; they held the profile's stock bytes
// Each pass calls stock's note pitch 8d5c0a70(track, note, x) (the voice start's call of the routine the edits
// change, 8d5c09d6) for tracks 0-7, notes 0-127 and x in 0-11 and 244-255, with stock's bytes at the two sites,
// then with the edits written there.
#include "../floor_hooks_sim/sim.hpp"

typedef u32 (*Convert)(u32);
typedef u32 (*Pitch)(u32, u32, u32);
static const u32 SITE1 = 0x8d5c0a1e, SITE2 = 0x8d5c0a2c;
static const u16 edit1[6] = { EDIT1 }, edit2[6] = { EDIT2 }, profile1[6] = { STOCK1 }, profile2[6] = { STOCK2 };
static u16 stock1[6], stock2[6];
static void put(u32 site, const u16* h) { for (u32 k = 0; k < 6; ++k) ((volatile u16*)site)[k] = h[k]; }
static void get(u32 site, u16* h) { for (u32 k = 0; k < 6; ++k) h[k] = ((volatile u16*)site)[k]; }
static const u32 N = 8 * 128 * 24;
static u32 before[N];

static u32 pass(unsigned* out, u32 w, u32 tag) {
    u32 n = 0, bad = 0, distinct = 0, ca = 0, cb = 0;
    put(SITE1, stock1); put(SITE2, stock2);
    for (u32 t = 0; t < 8; ++t) for (u32 note = 0; note < 128; ++note) for (u32 j = 0; j < 24; ++j) {
        const u32 x = j < 12 ? j : 232 + j;
        const u32 t0 = cycles();
        before[n] = ((Pitch)0x8d5c0a70)(t, note, x);
        ca += cycles() - t0;
        distinct += n && before[n] != before[n - 1];
        ++n;
    }
    put(SITE1, edit1); put(SITE2, edit2);
    n = 0;
    for (u32 t = 0; t < 8; ++t) for (u32 note = 0; note < 128; ++note) for (u32 j = 0; j < 24; ++j) {
        const u32 x = j < 12 ? j : 232 + j;
        const u32 t0 = cycles();
        const u32 v = ((Pitch)0x8d5c0a70)(t, note, x);
        cb += cycles() - t0;
        bad += v != before[n];
        ++n;
    }
    put(SITE1, stock1); put(SITE2, stock2);
    out[w++] = tag; out[w++] = n; out[w++] = bad; out[w++] = distinct; out[w++] = ca / n; out[w++] = cb / n;
    return w;
}

PROBE {
    u32 w = 0;
    bss_zero();
    stock_boot();
    get(SITE1, stock1); get(SITE2, stock2);
    out[w++] = 0xf2000001;
    out[w++] = ((Convert)0x8d5bf5ec)(0x44960000u);   // 1200.0f
    out[w++] = ((Convert)0x8d5bf5ec)(0x42c80000u);   // 100.0f
    out[w++] = ((Convert)0x8d5bf5ec)(0x44960000u);   // again: nothing carried between calls
    out[w++] = ((Convert)0x8d5bf5ec)(0x42c80000u);
    w = pass(out, w, 0xf2000002);
    // Random pitch tables (the per-track tuning words the routine reads), so its inputs move its result.
    seed = 0x5eed;
    for (u32 o = 0; o < 0x1a0; o += 4) V(0x86bdd830 + o) = (u32)(((i32)(rnd() % 2400u) - 1200) << 16) + (rnd() & 0xffff);
    for (u32 o = 0; o < 0x500; o += 4) V(0x86bdd9b0 + o) = rnd();
    w = pass(out, w, 0xf2000003);
    u16 now1[6], now2[6];
    get(SITE1, now1); get(SITE2, now2);
    out[w++] = 0xf2000004; out[w++] = sameb(now1, stock1, sizeof now1) && sameb(now2, stock2, sizeof now2);
    out[w++] = sameb(stock1, profile1, sizeof stock1) && sameb(stock2, profile2, sizeof stock2);
    return w;
}
