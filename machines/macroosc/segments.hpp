// Macro Osc's 14-segment characters, drawn with ordinary drawing nodes.
#pragma once
#include <opxy/draw.hpp>

namespace macroosc {
// A character is 14 nodes in the order a b c d e f g h j k l m n p. A mask lights
// segment s (0 = a) when bit 15 - s is set (models.hpp holds each model's masks).
const unsigned kSegments = 14;
// Paints `characters` characters' nodes, from node `first`: palette entry `lit`
// for each lit segment, `unlit` for the others. The segments are authored in
// place, so each node sits at the origin, unscaled and unturned.
template <class F>
inline void paint_segments(F& frame, unsigned first, const unsigned* masks, unsigned characters, unsigned lit,
                           unsigned unlit) {
  for (unsigned c = 0; c < characters; ++c)
    for (unsigned s = 0; s < kSegments; ++s)
      opxy::draw::place(frame, first + c * kSegments + s, 0, 0, opxy::draw::unit,
                        masks[c] << s & 0x8000 ? lit : unlit, opxy::draw::fill_and_stroke);
}
}  // namespace macroosc
