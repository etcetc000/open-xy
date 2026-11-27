#pragma once
#include <opxy/output_levels.hpp>
namespace probescope {
struct DisplayState { opxy::u32 frames; };
using Frame = opxy::draw::GlyphFrame<opxy::draw::ExtendedFrame<1, 32, 5, 0, 0, true, false, 1>, 2, 2, 1>;
}
