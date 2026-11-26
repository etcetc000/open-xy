#include "display.h"
namespace probelarge {
inline void display_reset(DisplayState& state) { state.position = 0; }
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    if (!input.reduced_motion) state.position = (state.position + static_cast<opxy::u32>(clamp(input.elapsed, 0, 102))) & 8191u;
    const opxy::i32 swing = static_cast<opxy::i32>(state.position >> 8) - 16;
    for (opxy::u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        path.key = 1 + (state.position >> 8);
        path.points[0] = {-40 * unit, 0};
        path.points[1] = {-20 * unit, (swing - 20) * unit};
        path.points[2] = {20 * unit, (20 - swing) * unit};
        path.points[3] = {40 * unit, 0};
        place(frame, p, static_cast<opxy::i32>(80 + p * 64) * unit, 110 * unit, unit, 1 + p % 3, stroke);
    }
}
}
