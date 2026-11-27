#pragma once
#include <opxy/audio_transport.hpp>

namespace probestagemeter {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::ExtendedFrame<2, 64, 2, 0, 16, false>;
struct DisplayState { opxy::u32 frames; };
}
