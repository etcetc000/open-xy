#pragma once
#include <opxy/encoder_activity.hpp>

namespace probeplayer {
// Matches resources.display: one affine cubic curve, a dot and a text slot in direct
// colours, with the encoders' activity.
using Frame = opxy::draw::RgbaFrame<opxy::draw::AffineCubicFrame<1, 4, 2, 1, 0> >;
struct DisplayState { opxy::u32 frames, note; };
}
