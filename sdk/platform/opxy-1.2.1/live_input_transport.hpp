#pragma once
#include "input_monitor.hpp"

namespace opxy { namespace platform { namespace live_input_transport {

constexpr u32 track = 12, words = 58, slot = 57;
// Bits 28–31 tag the word, 16–27 carry the Brain pitch classes, 6–15
// carry tuner controls and 0–5 carry the Tilt Mic lease epoch and gate.
constexpr u32 tag = 0x40000000u, tuner_mask = 0x0fffffc0u;
constexpr u32 option_bits(u32 target, u32 retune, u32 amount, u32 brain = 0) {
    return (target << 6) | (retune << 11) | ((15 - amount) << 12) | (brain << 16);
}
constexpr u32 target(u32 value) { return (value >> 6) & 31u; }
constexpr u32 retune(u32 value) { return (value >> 11) & 1u; }
constexpr u32 amount(u32 value) { return 15u - ((value >> 12) & 15u); }
constexpr u32 brain(u32 value) { return (value >> 16) & 0xfffu; }
constexpr bool monitoring(u32 value) {
    return (value & input_monitor::mask) == input_monitor::mark ||
           (value & input_monitor::mask) == input_monitor::released;
}
constexpr bool tagged(u32 value) {
    return (value & input_monitor::mask) == tag || monitoring(value);
}

}}}
