#pragma once
#include "input_monitor.hpp"
#include <opxy/live_input.hpp>

namespace opxy { namespace platform { namespace live_input_transport {

constexpr u32 track = 12, words = 58, slot = 57;
// Bits 28–31 tag the word, 21–27 carry the extra page, 16–20 carry
// the Brain scale index, 6–15 carry tuning and 0–5 carry the Tilt Mic lease.
constexpr u32 tag = 0x40000000u, tuner_mask = 0x0fffffc0u;
constexpr u32 option_bits(u32 target, u32 retune, u32 amount, u32 scale,
                          const live_input::ExtraValues& extra) {
    return (target << 6) | (retune << 11) | ((15 - amount) << 12) | (scale << 16) |
        (extra.values[0] << 21) | (extra.values[1] << 23) | (extra.values[2] << 26) |
        (extra.values[3] << 27);
}
constexpr u32 target(u32 value) { return (value >> 6) & 31u; }
constexpr u32 retune(u32 value) { return (value >> 11) & 1u; }
constexpr u32 amount(u32 value) { return 15u - ((value >> 12) & 15u); }
inline u32 brain(u32 value) {
    const u32 scale = (value >> 16) & 31u;
    if (!scale || scale > 24) return 0;
    const u32 base = (scale - 1) % 12;
    const u32 intervals = scale > 12 ? 0xb35u : 0xab5u;
    return ((intervals << base) | (intervals >> (12 - base))) & 0xfffu;
}
constexpr live_input::ExtraValues extra_values(u32 value) {
    return {{(value >> 21) & 3u, (value >> 23) & 7u, (value >> 26) & 1u, (value >> 27) & 1u}};
}
constexpr bool monitoring(u32 value) {
    return (value & input_monitor::mask) == input_monitor::mark ||
           (value & input_monitor::mask) == input_monitor::released;
}
constexpr bool tagged(u32 value) {
    return (value & input_monitor::mask) == tag || monitoring(value);
}

}}}
