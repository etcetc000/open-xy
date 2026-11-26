// floor_hooks.s against stock's own instructions on the BF707 Functional-Sim, stock OS 1.2.1 loaded at its
// own addresses (tests/test_floor_hooks.py). Output, in order:
//   0xf1000001 cases mismatches stock_cycles hook_cycles     sdk_lfo_pass vs stock 11a044e6..11a0451c
//   0xf1000002 cases mismatches (n stock hook) x 4             8d50fa88 (tape track) with and without the tape
//                                                              hooks: idle filter off, idle filter on, playing
//                                                              at gain 0, other
//   0xf1000003 blocks mismatches first stock_cycles hook_cycles  whole audio loop from start-up, per block
#include "sim.hpp"

// ---- LFO pass check
struct Ctx { u32 r0, r1, r2, r4, r5, r6, r7, p3, p4, p5, fp, cyc, sp; };
extern "C" void run_lfo(Ctx*, u32 which);
static u32 frame[64] __attribute__((aligned(4)));
// field(w) = EXTRACT(w >>> 8, 0x1509): 9 signed bits at bit 29 of w.
static u32 with_field(u32 r, i32 f) { return (r & ~(0x1ffu << 29)) | (((u32)f & 0x1ff) << 29); }
static const i32 interesting[] = {0, 1, 2, 3, 4, 5, 6, -1, 0x12, 0x20};
static u32 lfo(unsigned* out, u32 w) {
    u32 bad = 0, ca = 0, cb = 0, n = 0, keep[16];
    for (u32 t = 0; t < 16; ++t) keep[t] = V(0x84042700 + 4 * t);
    for (u32 iter = 0; iter < 3000; ++iter) {
        seed = 0x5eed0000u + iter * 2654435761u;
        u32 type = (rnd() % 4 == 0) ? rnd() : rnd() % 9;
        u32 p60 = rnd(), p68 = rnd(), p6c = rnd(), dw = rnd();
        if (rnd() & 1) p60 = with_field(p60, interesting[rnd() % 10]);
        if (rnd() & 1) p68 = with_field(p68, interesting[rnd() % 10]);
        if (rnd() & 1) p6c = with_field(p6c, interesting[rnd() % 10]);
        if (rnd() & 1) dw = with_field(dw, interesting[rnd() % 10]);
        if (rnd() % 3 == 0) p68 = (rnd() & 0xff) << 8 | (rnd() & 0x3) << 30;
        u32 engine = (rnd() & 1) ? (u32)interesting[rnd() % 10] : rnd();
        u32 track = rnd() % 16;
        for (u32 t = 0; t < 16; ++t) V(0x84042700 + 4 * t) = (rnd() & 1) ? 0x12 : rnd() % 32;
        // The registers and frame stock has at 11a044e6 (floor_hooks.s).
        Ctx c;
        c.r0 = type; c.r1 = p60; c.r2 = p68; c.r4 = track; c.r5 = type; c.r6 = rnd(); c.r7 = dw; c.p3 = rnd(); c.p4 = type; c.p5 = rnd();
        c.fp = (u32)&frame[48];
        for (u32 k = 0; k < 64; ++k) frame[k] = rnd();
        u32 sp = c.fp - 0x38;
        V(sp + 0xc) = p6c; V(sp + 0x10) = c.fp + 8; VB(c.fp + 8) = 0; V(c.fp + 0x10) = engine; V(sp + 0x34) = 0x1509; V(c.fp + 0x14) = rnd() % 6;
        u32 f0[64], fa[64], fb[64];
        copyb(f0, frame, sizeof frame);
        Ctx a = c; run_lfo(&a, 0); copyb(fa, frame, sizeof frame);
        copyb(frame, f0, sizeof frame);
        Ctx b = c; run_lfo(&b, 1); copyb(fb, frame, sizeof frame);
        // R1 is written before it is read on every path after 11a0451c; below SP is callees' space.
        bool ok = a.r0 == b.r0 && a.r4 == b.r4 && a.r5 == b.r5 && a.r6 == b.r6 && a.r7 == b.r7 && a.p3 == b.p3 &&
                  a.p4 == b.p4 && a.p5 == b.p5 && a.fp == b.fp && a.sp == b.sp;
        for (u32 k = (sp - (u32)frame) / 4; k < 64; ++k) if (fa[k] != fb[k]) ok = false;
        if (!ok) ++bad;
        ca += a.cyc; cb += b.cyc; ++n;
    }
    for (u32 t = 0; t < 16; ++t) V(0x84042700 + 4 * t) = keep[t];
    out[w++] = 0xf1000001; out[w++] = n; out[w++] = bad; out[w++] = ca / n; out[w++] = cb / n;
    return w;
}

// ---- Tape track, one call from the same memory
// What the tape track touches. The scratch buffers [842e2a74]/[842e2a78] are left out: only the gain loop
// reads them, after 8d503548 rewrites them.
struct R { u32 a, n; };
static const R TREG[] = {
    {0x84c6992c, 0x104}, {0x84c68eac, 0x40}, {0x84c68c00, 0x40}, {0x84c689f0, 0x20},
    {0x11903600, 0x200}, {0x11902e00, 0x100}, {0x84caa140, 0x200}, {0x84caa340, 0x200}, {0x84caa740, 0x200},
    {MASTER, 0x200}, {0x84ca9d90, 0x30}, {0x080376e4, 0x80}, {0x84039150, 0x10}, {0x8404cae8, 4}, {0x842e2a60, 0x20},
};
static const u32 NT = sizeof TREG / sizeof TREG[0];
static u8 snap0[0x1800], snapA[0x1800], snapB[0x1800];
static u32 save(u8* s) { u32 o = 0; for (u32 k = 0; k < NT; ++k) { copyb(s + o, (void*)TREG[k].a, TREG[k].n); o += TREG[k].n; } return o; }
static void load(const u8* s) { u32 o = 0; for (u32 k = 0; k < NT; ++k) { copyb((void*)TREG[k].a, s + o, TREG[k].n); o += TREG[k].n; } }
static i32 smp() { u32 r = rnd(); switch (r & 7) { case 0: return 0; case 1: return 0x7fffffff; case 2: return (i32)0x80000000; case 3: return (i32)(rnd() & 0xff) - 128; default: return (i32)rnd(); } }
static void scenario(u32 n, u32 prm) {
    seed = 0x7a9e0000u + n * 40503u;
    const u32 T = 0x84c6992c;
    u32 kind = n % 8;   // 0-3 idle (gain 0, not playing), 4 small gain fading out, 5 playing at gain 0, 6 playing, 7 random
    for (u32 o = 0x8; o < 0x10; o += 4) V(T + o) = rnd() & 0x3fffffff;
    V(T + 0xf4) = (rnd() & 1) ? rnd() & 0x7fffffff : (rnd() & 3) ? 0 : rnd();
    V(T + 0xf8) = (i32)(rnd() & 0xffff) - 0x8000 + ((rnd() & 3) == 0 ? (i32)rnd() : 0);
    V(T + 0xf0) = kind < 4 || kind == 5 ? 0 : kind == 4 ? (rnd() & 0xff) : rnd();
    VB(T + 0x100) = kind >= 5 ? 1 : 0;
    if (kind == 7) VB(T + 0x100) = rnd() & 1;
    for (u32 o = 0; o < 0xe8; o += 4) V(prm + o) = rnd();   // the tape track's parameters
    if (n % 3) V(prm + 0x58) = 0;
    for (u32 k = 0; k < 512; ++k) {   // tape memory under both heads
        V(0x842e327c + ((V(T + 8) >> 10) + k) % 0x100000 * 4) = smp();
        V(0x844e327c + ((V(T + 8) >> 10) + k) % 0x80000 * 4) = smp();
    }
    for (u32 k = 0; k < 128; ++k) {
        V(0x84caa140 + 4 * k) = smp(); V(0x84caa340 + 4 * k) = smp(); V(MASTER + 4 * k) = smp();
        V(0x84caa740 + 4 * k) = smp(); V(0x11903600 + 4 * k) = smp(); V(0x11902e00 + 4 * (k & 63)) = rnd();
    }
    for (u32 b = 0x84c68c08; b <= 0x84c68c14; b += 0xc) {   // level and pan ramps: step, value, shift, snap
        V(b) = (rnd() & 1) ? 0 : smp(); V(b + 4) = smp(); *(volatile u16*)(b + 8) = rnd() % 12; VB(b + 0xa) = (rnd() % 4) == 0;
    }
    VB(0x11b01dd5) = (n % 5 == 4);   // the tape filter
    for (u32 o = 0; o < 0x30; o += 4) V(0x84c68eac + o) = (rnd() & 1) ? 0 : smp();
    *(volatile u16*)(0x84c68eac + 8) = rnd() % 12; *(volatile u16*)(0x84c68eac + 0x14) = rnd() % 12;
    VB(0x84c68eac + 10) = rnd() & 1; VB(0x84c68eac + 0x16) = rnd() & 1;
    for (u32 o = 0; o < 0x18; o += 4) V(0x84ca9d9c + o) = smp();   // the tape track's meter
}
// The tape track through call_tape (harness.s): every register but the two arguments from regv, the same for
// stock and the hooks. Stock's tape machine can read the caller's R4 before setting it; in the firmware that is
// the audio loop's own R4 either way, here it must not differ between the two calls.
extern "C" void call_tape();
extern "C" { u32 regv[30]; }
static u32 tape(unsigned* out, u32 w) {
    const u32 prm = 0x84eab39c + 13 * 0xe8, N = 400;
    u32 bad = 0, ca[4] = {0}, cb[4] = {0}, cn[4] = {0};
    for (u32 n = 0; n < N; ++n) {
        scenario(n, prm);
        for (u32 r = 0; r < 30; ++r) regv[r] = rnd();
        for (u32 r = 6; r < 16; ++r) regv[r] = 0x87f00000 + (r - 6) * 0x1000;   // P0-P5, I0-I3: harmless addresses
        for (u32 r = 20; r < 24; ++r) regv[r] = 0x87f0c000 + (r - 20) * 0x1000;  // B0-B3
        regv[24] = regv[25] = 0;                                                 // LC0, LC1
        u32 sz = save(snap0);
        u32 t0 = cycles(); call_tape(); u32 a = cycles() - t0; save(snapA);
        load(snap0); tape_hooks_on();
        t0 = cycles(); call_tape(); u32 b = cycles() - t0; save(snapB);
        tape_hooks_off();
        if (!sameb(snapA, snapB, sz)) ++bad;
        u32 c = n % 8 < 4 ? (VB(0x11b01dd5) ? 1 : 0) : n % 8 == 5 ? 2 : 3;
        ca[c] += a; cb[c] += b; ++cn[c];
    }
    out[w++] = 0xf1000002; out[w++] = N; out[w++] = bad;
    for (u32 c = 0; c < 4; ++c) { out[w++] = cn[c]; out[w++] = cn[c] ? ca[c] / cn[c] : 0; out[w++] = cn[c] ? cb[c] / cn[c] : 0; }
    return w;
}

// ---- Whole audio loop from start-up, stock then the tape hooks: per-block hashes of the audio state.
static const R HREG[] = {
    {0x84c60000, 0x50000}, {0x11900000, 0x8000}, {0x11b00000, 0x2000}, {0x84eab39c, 0x4064}, {0x84039000, 0x1000},
    {0x08037600, 0x200}, {0x8404c000, 0x2000}, {0x842e2a00, 0x100}, {0x84042600, 0x200}, {0x84047900, 0x100},
    {0x84052f00, 0x500},
};
static u32 hash() {
    u32 h = 2166136261u;
    for (u32 k = 0; k < sizeof HREG / sizeof HREG[0]; ++k)
        for (u32 p = HREG[k].a; p < HREG[k].a + HREG[k].n; p += 4)
            if (p != 0x84c69918) h = (h ^ V(p)) * 16777619u;   // 84c69918: stock's smoothed CPU load (from CYCLES)
    return h;
}
static const u32 K = 160;   // past the tape's start-up clear (about 128 blocks), so playback's hook runs too
static u32 ha[K], hb[K], ca[K], cb[K];
static u32 whole(unsigned* out, u32 w) {
    stock_boot();
    for (u32 b = 0; b < K; ++b) { u32 t0 = cycles(); audio_block(); ca[b] = cycles() - t0; ha[b] = hash(); }
    stock_boot();
    tape_hooks_on();
    for (u32 b = 0; b < K; ++b) { u32 t0 = cycles(); audio_block(); cb[b] = cycles() - t0; hb[b] = hash(); }
    tape_hooks_off();
    u32 bad = 0, first = 0xffffffff, sa = 0, sb = 0;
    for (u32 b = 0; b < K; ++b) if (ha[b] != hb[b]) { ++bad; if (first == 0xffffffff) first = b; }
    for (u32 b = K - 16; b < K; ++b) { sa += ca[b]; sb += cb[b]; }
    out[w++] = 0xf1000003; out[w++] = K; out[w++] = bad; out[w++] = first; out[w++] = sa / 16; out[w++] = sb / 16;
    return w;
}

PROBE {
    u32 w = 0;
    bss_zero();
    stock_boot();
    w = lfo(out, w);
    for (u32 b = 0; b < 140; ++b) audio_block();   // past the start-up clear
    w = tape(out, w);
    w = whole(out, w);
    return w;
}
