#pragma once
#include "input_monitor.hpp"
#include <opxy/live_input.hpp>

namespace opxy { namespace platform { namespace live_input_transport {

constexpr u32 track = 12, words = 58, slot = 57;
// Bits 29–31 tag the word, 24–28 carry stock's scale context, 6–23
// hold the declared choice indices, and 0–5 carry the monitor lease.
constexpr u32 tag = 0x40000000u, option_mask = 0x1fffffc0u;
inline u32 option_bits(const live_input::Controls& controls, const u32* choices, u32 scale) {
    u32 result = scale << 24;
    u32 shift = 6;
    for (u32 row = 0; row < live_input::row_count; ++row) {
        const auto& spec = controls.rows[row];
        result |= (choices[row] ^ spec.initial) << shift;
        shift += live_input::width(spec.count);
    }
    return result;
}
inline u32 brain(u32 value) {
    const u32 scale = (value >> 24) & 31u;
    if (!scale || scale > 24) return 0;
    const u32 base = (scale - 1) % 12;
    const u32 intervals = scale > 12 ? 0xb35u : 0xab5u;
    return ((intervals << base) | (intervals >> (12 - base))) & 0xfffu;
}
inline live_input::Parameters parameters(const live_input::Controls& controls, u32 value) {
    live_input::Parameters result = {};
    result.brain = brain(value);
    u32 shift = 6;
    for (u32 row = 0; row < live_input::row_count; ++row) {
        const auto& spec = controls.rows[row];
        const u32 bits = live_input::width(spec.count);
        const u32 choice = ((value >> shift) & ((1u << bits) - 1u)) ^ spec.initial;
        result.choices[row] = choice < spec.count ? choice : spec.initial;
        shift += bits;
    }
    return result;
}
constexpr bool monitoring(u32 value) {
    return (value & input_monitor::mask) == input_monitor::mark ||
           (value & input_monitor::mask) == input_monitor::released;
}
constexpr bool tagged(u32 value) {
    return (value & input_monitor::mask) == tag || monitoring(value);
}

}}}
