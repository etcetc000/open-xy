// Host doubles for the drawing input's stock reads: the amp envelope
// (tests/fixtures/amp_envelope.cpp exercises the real one) and the project tempo.
#pragma once
#include <opxy/draw.hpp>
namespace opxy { namespace platform {
inline void amp_envelope(u32 track, draw::Envelope& amp) {
    amp.attack = static_cast<i32>(track) + 1;
    amp.decay = 2; amp.sustain = 3; amp.release = 4;
}
inline u32 project_bpm_x10() { return 1375; }
}}
