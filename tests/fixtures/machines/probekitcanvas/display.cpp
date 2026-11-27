#include "display.h"

namespace probekitcanvas {
inline void display_reset(DisplayState& state) { state.hits = 0; }

// The whole screen: a square for the selected key (0..23 across the screen), a line
// to the last played pad's key, lifted while an encoder moves, and a dot whose colour
// is the selected key's volume (choices[2]).
inline void display(DisplayState& state, const opxy::draw::EncoderInput& input, Frame& frame) {
    using namespace opxy::draw;
    if (input.trigger_key) ++state.hits;
    const opxy::i32 key = static_cast<opxy::i32>(input.selected_key < 24 ? input.selected_key : 0);
    const opxy::i32 played = input.trigger_key >= 53 && input.trigger_key < 77
        ? static_cast<opxy::i32>(input.trigger_key - 53) : key;
    const opxy::i32 lift = input.encoders.available && input.encoders.mask ? -20 : 0;
    for (opxy::u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        path.key = 1;
        path.points[0] = {-8 * unit, -8 * unit};
        path.points[1] = {8 * unit, -8 * unit};
        path.points[2] = {8 * unit, 8 * unit};
        path.points[3] = {-8 * unit, 8 * unit};
    }
    frame.paths[1].key = 1 + static_cast<opxy::u32>(played);
    frame.paths[1].points[2] = {static_cast<opxy::i32>((played - key) * 18) * unit, 0};
    affine(frame, 0, Matrix{1024, 0, 0, 1024, (24 + 18 * key) * unit, (110 + lift) * unit});
    affine(frame, 1, Matrix{1024, 0, 0, 1024, (24 + 18 * key) * unit, 150 * unit});
    affine(frame, 2, Matrix{1024, 0, 0, 1024, 440 * unit, 40 * unit});
    rgba(frame, 0, color(255, 255, 255, 255));
    rgba(frame, 1, color(120, 200, 255, 255));
    const opxy::u32 volume = input.choices[2] > 50 ? 50 : input.choices[2];
    rgba(frame, 2, color(static_cast<opxy::u8>(5 * volume), static_cast<opxy::u8>(5 * volume), static_cast<opxy::u8>(5 * volume)));
}
}
