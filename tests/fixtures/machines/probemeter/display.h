#pragma once
#include <opxy/audio_transport.hpp>
#include <opxy/output_levels.hpp>
namespace probemeter {
struct DisplayState { opxy::u32 serial; };
using Frame = opxy::draw::AffineQuadraticFrame<2, 16, 2, 0, 0>;
}
