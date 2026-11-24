// Bit-exactness and cycle probe for the track mixing hook on the BF707 Functional-Sim.
// Stock's own instructions (relocated byte copies of OS 1.2.1, harness.s) against the SDK's
// replacements, from identical memory, with the same stubs. Freestanding.
typedef unsigned u32; typedef int i32; typedef unsigned char u8;
static inline u32 cycles() { u32 n; __asm__ volatile("%0 = CYCLES;" : "=d"(n)::"memory"); return n; }
static u32 seed;
static u32 rnd() { seed = seed * 1664525u + 1013904223u; return seed ^ (seed >> 15); }
static i32 sample() {
    u32 r = rnd();
    switch (r & 7) {
    case 0: return 0;
    case 1: return 0x7fffffff - (i32)(rnd() & 3);
    case 2: return (i32)0x80000000 + (i32)(rnd() & 3);
    case 3: return (i32)(rnd() & 0xff) - 128;
    default: return (i32)rnd();
    }
}
#define V(addr) (*(volatile u32*)(addr))
#define A(addr) ((i32*)(addr))
static void copy(void* d, const void* s, u32 n) { for (u32 i = 0; i < n; ++i) ((volatile u8*)d)[i] = ((const volatile u8*)s)[i]; }
static bool same(const void* a, const void* b, u32 n) { for (u32 i = 0; i < n; ++i) if (((const u8*)a)[i] != ((const u8*)b)[i]) return false; return true; }

// ---------------- Item 1: the five bus mixes ----------------
struct Ctx { i32 r1, r5, r6, r7; u32 p3_out, fp, sp, cycles; };
extern "C" void run_mix(Ctx*, u32 which);
static const u32 BUF = 0x11903600, TAPE = 0x84caa740, FX1 = 0x84caa140, FX2 = 0x84caa340;
static i32 aux[128], master[128], frame[96], spf[64], pan[16];
struct Ramp { i32 step, gain; short shift; u8 snap, pad; } __attribute__((aligned(4)));
static Ramp ramp;
struct MixState { i32 buf[128], aux[128], tape[128], fx1[128], fx2[128], master[128], frame[96], spf[64], pan[16]; Ramp ramp; };
static MixState s0, sa, sb;
static void save(MixState& m) {
    copy(m.buf, A(BUF), 512); copy(m.aux, aux, 512); copy(m.tape, A(TAPE), 512); copy(m.fx1, A(FX1), 512);
    copy(m.fx2, A(FX2), 512); copy(m.master, master, 512); copy(m.frame, frame, sizeof frame); copy(m.spf, spf, sizeof spf);
    copy(m.pan, pan, sizeof pan); copy(&m.ramp, &ramp, sizeof ramp);
}
static void load(const MixState& m) {
    copy(A(BUF), m.buf, 512); copy(aux, m.aux, 512); copy(A(TAPE), m.tape, 512); copy(A(FX1), m.fx1, 512);
    copy(A(FX2), m.fx2, 512); copy(master, m.master, 512); copy(frame, m.frame, sizeof frame); copy(spf, m.spf, sizeof spf);
    copy(pan, m.pan, sizeof pan); copy(&ramp, &m.ramp, sizeof ramp);
}
static i32 mul(i32 a, i32 b) { long long p = (long long)a * b; long long r = p >> 31; return r > 0x7fffffff ? 0x7fffffff : (i32)r; }
static i32 shl11(i32 x) { long long r = (long long)x << 11; return r > 0x7fffffff ? 0x7fffffff : r < -0x7fffffffll - 1 ? (i32)0x80000000 : (i32)r; }
static i32* F(i32 off) { return (i32*)((u8*)&frame[64] + off); }   // FP = &frame[64]
static i32* S(i32 off) { return (i32*)((u8*)&spf[16] + off); }     // SP = &spf[16]

static void mix_scenario(u32 n, Ctx& c) {
    seed = 0x1234567u + n * 7919u;
    u32 kind = n % 8;
    for (u32 i = 0; i < 128; ++i) {
        i32 x = sample();
        if (kind == 0) x = 0;                       // all zero
        if (kind == 1 && i < 127) x = 0;            // only the last word
        if (kind == 2 && i != 0) x = 0;             // only the first word
        if (kind == 3 && i < 64) x = 0;             // second half
        A(BUF)[i] = x;
        aux[i] = sample(); A(TAPE)[i] = sample(); A(FX1)[i] = sample(); A(FX2)[i] = sample(); master[i] = sample();
    }
    for (u32 i = 0; i < 96; ++i) frame[i] = (i32)rnd();
    for (u32 i = 0; i < 64; ++i) spf[i] = (i32)rnd();
    for (u32 i = 0; i < 16; ++i) pan[i] = sample();
    u32 g = rnd();
    c.r5 = (g & 3) == 0 ? 0 : sample();
    c.r6 = (g & 12) == 0 ? 0 : sample();
    c.r7 = (g & 48) == 0 ? 0 : sample();
    *F(-8) = (g & 192) == 0 ? 0 : sample();
    *F(-0xc) = (n % 13 == 5) ? sample() : 0;        // stock always stores 0 here
    *F(-0x50) = (g & 0x300) == 0 ? 0 : sample();
    *F(-0x7c) = (i32)(u32)aux;
    *F(0xc) = (i32)(u32)&pan[6];
    *S(0x48) = (i32)((rnd() % 5) * 4) - 8;
    *F(-0x34) = (i32)(u32)&ramp;
    *S(0x58) = (i32)(u32)master;
    ramp.step = sample(); ramp.gain = (g & 0xc00) == 0 ? 0 : sample();
    ramp.shift = (short)(rnd() % 12); if (n % 17 == 3) ramp.shift = 31;
    ramp.snap = (g & 0x3000) == 0 ? 1 : 0; ramp.pad = (u8)rnd();
    if ((g & 0xc000) == 0) { *F(-0x50) = 0; ramp.gain = 0; ramp.snap = 0; }   // silent master
    if (n % 11 == 4) { pan[6] = 0x7fffffff; *F(-0x50) = 0x7fffffff; ramp.gain = 0; ramp.snap = 0; ramp.shift = 0; }
    c.r1 = shl11(mul(c.r5, A(BUF)[0]));
    c.fp = (u32)&frame[64]; c.sp = (u32)&spf[16];
}

static u32 mix_probe(u32* out) {
    u32 w = 0, bad = 0, sum_a = 0, sum_b = 0, sil_a = 0, sil_b = 0, nsil = 0;
    const u32 N = 400;
    for (u32 n = 0; n < N; ++n) {
        Ctx c;
        mix_scenario(n, c); save(s0);
        Ctx ca = c; run_mix(&ca, 0); save(sa);
        load(s0); Ctx cb = c; run_mix(&cb, 1); save(sb);
        const bool ok = same(&sa, &sb, sizeof sa) && ca.p3_out == cb.p3_out;
        if (!ok) { ++bad; if (w < 40) { out[w++] = 0xbad00000u | n; } }
        sum_a += ca.cycles; sum_b += cb.cycles;
        if (n % 8 == 0) { sil_a += ca.cycles; sil_b += cb.cycles; ++nsil; }
        if (n < 16) { out[w++] = 0xc0000000u | n; out[w++] = ca.cycles; out[w++] = cb.cycles; }
    }
    out[w++] = 0xaaaa0001; out[w++] = N; out[w++] = bad; out[w++] = sum_a / N; out[w++] = sum_b / N;
    out[w++] = sil_a / nsil; out[w++] = sil_b / nsil;
    // Named cases: 0 silent, 1 sound with every gain nonzero, 2 sound to the master only,
    // 3 sound starting at word 120 with every gain nonzero (the longest scan before sound).
    out[w++] = 0xaaaa0002;
    for (u32 k = 0; k < 4; ++k) {
        Ctx c; mix_scenario(64 + 8 * k + 4, c);   // kind 4: random sound
        for (u32 i = 0; i < 128; ++i) { if (A(BUF)[i] == 0) A(BUF)[i] = 5; if (k == 0) A(BUF)[i] = 0; if (k == 3 && i < 120) A(BUF)[i] = 0; }
        i32 g = 0x40000000;
        c.r5 = k == 2 ? 0 : g; c.r6 = k == 2 ? 0 : g; c.r7 = k == 2 ? 0 : g; *F(-8) = k == 2 ? 0 : g; *F(-0xc) = 0;
        *F(-0x50) = g; pan[6] = g; ramp.snap = 0; ramp.shift = 7; ramp.gain = g;
        c.r1 = shl11(mul(c.r5, A(BUF)[0]));
        save(s0);
        Ctx ca = c; run_mix(&ca, 0); save(sa);
        load(s0); Ctx cb = c; run_mix(&cb, 1); save(sb);
        out[w++] = ca.cycles; out[w++] = cb.cycles; out[w++] = same(&sa, &sb, sizeof sa) && ca.p3_out == cb.p3_out;
    }
    return w;
}

// ---------------- the tap timer, skipped without profiling ----------------
// Stock enters 8d510f04 after the monitor tap: its stage timer end for slot 17
// (08035228, 0x2c bytes), then the five mixes. The profiling-off hook jumps from
// 8d510f04 straight to sdk_track_mix. Every word but the slot must agree.
static const u32 SLOT = 0x08035228;
static u32 tap_probe(u32* out) {
    u32 w = 0, bad = 0, counted = 0, sum[4] = {0};
    const u32 N = 160;
    for (u32 n = 0; n < N; ++n) {
        Ctx c;
        mix_scenario(n, c);
        // The slot as profiling leaves it: a start a little before now, max/min either side.
        const u32 now = cycles();
        V(SLOT) = now - 200 - rnd() % 4000; V(SLOT + 4) = 0;
        for (u32 k = 2; k < 10; ++k) V(SLOT + 4 * k) = (k & 1) ? 0 : rnd() % 8000;
        if (n & 1) { V(SLOT + 0x10) = 0; V(SLOT + 0x18) = 0xffffffff; V(SLOT + 0x1c) = 0x7fffffff; }
        V(SLOT + 0x28) = rnd() % 1000;
        *S(0x60) = (i32)(SLOT + 0x10); *S(0x64) = (i32)(SLOT + 0x18); *F(-0x78) = (i32)(SLOT + 0x20);
        const u32 count = V(SLOT + 0x28);
        save(s0);
        Ctx ca = c; run_mix(&ca, 2); save(sa);
        if (V(SLOT + 0x28) == count + 1) ++counted;   // stock's bytes ran: the count moved
        load(s0); Ctx cb = c; run_mix(&cb, 3); save(sb);
        if (!same(&sa, &sb, sizeof sa) || ca.p3_out != cb.p3_out) { ++bad; if (w < 40) out[w++] = 0xbad20000u | n; }
        load(s0); Ctx c0 = c; run_mix(&c0, 0);
        load(s0); Ctx c1 = c; run_mix(&c1, 1);
        sum[0] += ca.cycles; sum[1] += cb.cycles; sum[2] += c0.cycles; sum[3] += c1.cycles;
    }
    out[w++] = 0xaaaa0004; out[w++] = N; out[w++] = bad; out[w++] = counted;
    for (u32 k = 0; k < 4; ++k) out[w++] = sum[k] / N;
    return w;
}

extern "C" unsigned sim_probe(unsigned* out) {
    u32 w = mix_probe(out);
    w += tap_probe(out + w);
    return w;
}
