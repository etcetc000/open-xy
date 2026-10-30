// Explicit diagnostics only. Never called by the normal audio callbacks.
#pragma once
#include <opxy/transport.hpp>
#include <opxy/sample.hpp>
extern "C" opxy::u32 sdk_benchmark_cycles() __asm__("sdk_benchmark_cycles");

namespace opxy { namespace benchmark {
struct Stats {
    u32 maximum, minimum, total_low, total_high, count, audible;
    void add(u32 elapsed) {
        if (!count || elapsed < minimum) minimum = elapsed;
        if (elapsed > maximum) maximum = elapsed;
        const u32 before = total_low;
        total_low += elapsed;
        total_high += total_low < before;
        ++count;
    }
    void observe(const i32* output, u32 words) {
        for (u32 i = 0; i < words; ++i) audible |= static_cast<u32>(output[i]);
    }
};
inline u32 cycles() { return sdk_benchmark_cycles(); }
// Same bounded stock heap calls used by sample_cache_stock.cpp; setup/cleanup
// run with interrupts enabled, outside the measured callbacks.
void* allocate(u32 bytes);
void release(void* block);
struct Memory {
    void* data;
    explicit Memory(u32 bytes) : data(bytes ? allocate(bytes) : nullptr) {}
    ~Memory() { if (data) release(data); }
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;
};
const u32 blocks = 1500, sample_frames = 88200;
// Deterministic non-silent fixture: half-scale triangle with periodic impulses.
inline i16 pcm(u32 frame) {
    const u32 phase = (frame * 97u) & 65535u;
    const i32 triangle = phase < 32768u ? static_cast<i32>(phase) : 65535 - static_cast<i32>(phase);
    return static_cast<i16>(frame % 11025u == 0 ? 24576 : triangle - 16384);
}
inline Transport transport(u32 block) { return Transport{1200, block * 64u * 192u / 44100u, true}; }
}}
