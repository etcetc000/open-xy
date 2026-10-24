#pragma once
#include <opxy/draw.hpp>

namespace fm2op {
// Two outlines, 24 pulse rings and two outline shadows; no text; 16 palette entries.
using Frame = opxy::draw::Frame<2, 64, 28, 0, 16>;
struct DisplayState {
    opxy::u32 phase, previous_event, track, next_slot, seed, initialized;
    opxy::i32 jitter_age;
    opxy::i32 ages[4];
    opxy::i32 decays[4];
};
}
