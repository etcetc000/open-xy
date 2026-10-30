#include "display.h"

namespace probesynth {
inline void display_reset(DisplayState& state) { state.phase = 0; }

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    if (!input.reduced_motion)
        state.phase = (state.phase + static_cast<opxy::u32>(clamp(input.elapsed, 0, 102))) & 8191u;
    const opxy::i32 motion = input.reduced_motion ? 0
        : (static_cast<opxy::i32>(state.phase >> 7) - 32) * (input.parameters[3] >> 26) / 32;
    for (opxy::u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        path.key = 1;
        for (opxy::u32 i = 0; i < Frame::point_count; ++i) {
            const opxy::i32 step = static_cast<opxy::i32>(i % 16) * 4;
            path.points[i].x = (i < 16 ? -32 + step : i < 32 ? 32 : i < 48 ? 32 - step : -32) * unit;
            path.points[i].y = (i < 16 ? -32 : i < 32 ? -32 + step : i < 48 ? 32 : 32 - step) * unit;
        }
    }
    place(frame, 0, (208 + motion) * unit, 120 * unit, unit, 15, stroke);
    place(frame, 1, (272 - motion) * unit, 120 * unit, unit, 10, stroke);
}
}
