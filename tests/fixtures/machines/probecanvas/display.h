#pragma once
#include <opxy/encoder_activity.hpp>
namespace probecanvas {
struct DisplayState { opxy::u32 serial; };
// Paths 0 and 1 open, 2 and 3 separate segments.
using Frame = opxy::draw::RgbaFrame<opxy::draw::AffineCountedFrame<4, 80, 4, 0, 0, 3, 12>>;
}
