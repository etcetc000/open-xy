#pragma once
#include <opxy/draw.hpp>

namespace probeident {
// Matches resources.display in definition.json: no paths, one node, four text slots.
using Frame = opxy::draw::Frame<0, 0, 1, 4, 2>;
struct DisplayState { opxy::u32 frames; };
}
