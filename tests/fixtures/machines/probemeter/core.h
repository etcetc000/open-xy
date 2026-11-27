#pragma once
#include <opxy/effect.hpp>
#include <opxy/telemetry.hpp>

namespace probemeter {
// At most opxy::effect_state_bytes; storage starts zeroed and is never destroyed.
struct State { opxy::i32 level; };
void initialize(State& state);
void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters parameters,
             const opxy::AudioContext& audio);
}
