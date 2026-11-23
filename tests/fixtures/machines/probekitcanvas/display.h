#pragma once
#include <opxy/encoder_activity.hpp>

namespace probekitcanvas {
// Matches resources.display: two affine paths of four points (path 1 open), a third
// node, direct colours, the whole screen and the encoders' activity.
using Frame = opxy::draw::RgbaFrame<opxy::draw::AffineFrame<2, 4, 3, 0, 0, 2> >;
struct DisplayState { opxy::u32 hits; };
}
