#pragma once
#include <opxy/draw.hpp>

namespace probesources {
// Matches resources.display in definition.json: no paths, two nodes, two palette entries.
using Frame = opxy::draw::Frame<0, 0, 2, 0, 2>;
struct DisplayState { opxy::u32 frames; };
}
