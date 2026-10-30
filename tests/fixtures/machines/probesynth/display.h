#pragma once
#include <opxy/draw.hpp>

namespace probesynth {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<2, 64, 2, 0, 16>;
struct DisplayState { opxy::u32 phase; };
}
