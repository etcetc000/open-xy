#pragma once
#include <opxy/draw.hpp>

namespace macroosc {
// Four 14-segment characters drawn four times (the segments, then three glow
// layers underneath), then four dials: 228 nodes. Each layer has a lit and an
// unlit palette entry. No paths and no text slots.
const unsigned kCharacters = 4, kLayers = 4, kDialNode = 224;
// Each dial's pivot in the artwork (Q10 pixels): Timbre, Color, FM Ratio, FM Depth.
const opxy::i32 kDialX[4] = {106015, 242207, 379423, 379423}, kDialY[4] = {176 << 10, 176 << 10, 176 << 10, 78 << 10};
using Frame = opxy::draw::Frame<0, 0, 228, 0, 8>;
// The model shown, plus one; 0 before the first frame.
struct DisplayState { opxy::u32 model; };
}  // namespace macroosc
