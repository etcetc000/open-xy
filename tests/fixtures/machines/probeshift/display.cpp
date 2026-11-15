#include "display.h"

namespace probeshift {
inline void display_reset(DisplayState& state) { state.phase = 0; state.header.reset(); }

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    const char* const titles[8] = {"Blend", "Width", "Level", "Motion", "Drive", "Crush", "Rectify", "Smooth"};
    char values[8][header_capacity];
    for (opxy::u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    // The Shift row: entry i under page i / 4, position i % 4; entry 5 is absent.
    const ShiftRow& row = shift_row(input);
    const char* const shift_titles[8] = {"Noise", "Drift", "Octave", "Lowcut", "Slide", "", "Ratio", "Balance"};
    const char* const octaves[5] = {"-2", "-1", "0", "+1", "+2"};
    char shift_values[8][header_capacity];
    for (opxy::u32 i = 0; i < 8; ++i) header_amount(shift_values[i], header_capacity, row.values[i]);
    copy(shift_values[2], header_capacity, octaves[((static_cast<opxy::u32>(row.values[2]) >> 16) * 5) >> 15]);
    shift_values[5][0] = 0;
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, 2, slots, row, shift_titles, shift_values);
    if (input.held_notes > 0 && input.held_notes <= 128 && !input.reduced_motion)
        state.phase = (state.phase + static_cast<opxy::u32>(clamp(input.elapsed, 0, 102))) & 8191u;
    // Drift (entry 1) widens the motion, as the noise it adds widens the sound.
    const opxy::i32 motion = input.reduced_motion ? 0
        : (static_cast<opxy::i32>(state.phase >> 7) - 32) * ((input.slots[3] >> 27) + (row.values[1] >> 27)) / 32;
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
