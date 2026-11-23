// Stock OS 1.2.1 loaded at its own addresses on the BF707 Functional-Sim (tests/test_floor_hooks.py):
// helpers, stock's start-up (BSS zero, constructor table), one audio block, the hook encodings.
#pragma once
typedef unsigned u32; typedef int i32; typedef unsigned char u8; typedef unsigned short u16;
#define V(a) (*(volatile u32*)(a))
#define VB(a) (*(volatile u8*)(a))
static inline u32 cycles() { u32 n; __asm__ volatile("%0 = CYCLES;" : "=d"(n)::"memory"); return n; }
// The GNU-linked probe's .bss is not in the loaded image: zero it (the firmware's zero fill does that for the module).
extern u32 sim_bss_start[] __asm__("__sim_bss_start"), sim_bss_end[] __asm__("__sim_bss_end");
static inline void bss_zero() { for (volatile u32* p = sim_bss_start; p < sim_bss_end; ++p) *p = 0; }
static u32 seed;
static inline u32 rnd() { seed = seed * 1664525u + 1013904223u; return seed ^ (seed >> 15); }
static void copyb(void* d, const void* s, u32 n) { for (u32 i = 0; i < n; ++i) ((volatile u8*)d)[i] = ((const volatile u8*)s)[i]; }
static bool sameb(const void* a, const void* b, u32 n) { for (u32 i = 0; i < n; ++i) if (((const volatile u8*)a)[i] != ((const volatile u8*)b)[i]) return false; return true; }
#define PROBE extern "C" unsigned sim_probe(unsigned* out) __attribute__((section(".text.sim_probe"))); extern "C" unsigned sim_probe(unsigned* out)

static void zero(u32 a, u32 b) { for (volatile u32* p = (volatile u32*)(a & ~3u); (u32)p < b; ++p) *p = 0; }
// Stock's initialized data as loaded, kept at 0x87800000 by the first start-up so a second one starts from
// the same memory.
struct Span { u32 a, b; };
static const Span INIT[] = { {0x84000000, 0x840c609c}, {0x08002000, 0x08002500}, {0x0800f500, 0x08040000},
                             {0x11800000, 0x11802110}, {0x11900000, 0x119059a8}, {0x11b00000, 0x11b01ed0} };
static bool booted;
static void copyw(u32 d, u32 s, u32 n) { for (u32 i = 0; i < n; i += 4) V(d + i) = V(s + i); }
static void stock_boot() {
    u32 keep = 0x87800000;
    if (!booted) { for (const Span& r : INIT) { copyw(keep, r.a, r.b - r.a); keep += r.b - r.a; } booted = true; }
    else for (const Span& r : INIT) { copyw(r.a, keep, r.b - r.a); keep += r.b - r.a; }
    zero(0x840c609c, 0x86c08620);
    zero(0x8c0d0380, 0x8d0d0380);   // the module area 8d0d0380.. holds this probe
    zero(0x080100d4, 0x0803c000);
    for (u32 a = 0x08002004; V(a); a += 4) ((void(*)())V(a))();
}
static const u32 ENGINE = 0x84c696fc, MASTER = 0x11b00948, AUXB = 0x11b00a48;
static void audio_block() { ((void(*)(u32, u32, u32))0x8d510760)(ENGINE, MASTER, AUXB); }

// The firmware's encodings (tooling/firmware.py): a 4-byte relative jump, an 8-byte absolute jump.
extern char sdk_tape_play[] __asm__("sdk_tape_play"); extern char sdk_tape_tail[] __asm__("sdk_tape_tail");
static void put16(u32 a, u16 v) { *(volatile u16*)a = v; }
static void rel_jump(u32 site, u32 target) {
    u32 d = ((target - site) / 2) & 0xffffff;
    put16(site, 0xe200 | (d >> 16)); put16(site + 2, d & 0xffff);
}
static void abs_jump(u32 site, u32 target) {
    put16(site, 0xdc00); put16(site + 2, target >> 16); put16(site + 4, target & 0xffff); put16(site + 6, 0);
}
static u32 saved_play, saved_tail[2];
static void tape_hooks_on() {
    saved_play = V(0x8d503878); saved_tail[0] = V(0x8d50fac4); saved_tail[1] = V(0x8d50fac8);
    rel_jump(0x8d503878, (u32)sdk_tape_play);
    abs_jump(0x8d50fac4, (u32)sdk_tape_tail);
}
static void tape_hooks_off() { V(0x8d503878) = saved_play; V(0x8d50fac4) = saved_tail[0]; V(0x8d50fac8) = saved_tail[1]; }
