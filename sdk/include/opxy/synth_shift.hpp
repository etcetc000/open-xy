#pragma once
#include "draw.hpp"

namespace opxy { namespace draw {
const u32 shift_entries = 8;

// A synth's Shift row (SDK 1.13, docs/display.md#a-synths-shift-row) as its page
// shows it. held is 1 while Shift is held on the engine page, when the encoders
// turn the row; line is the page the encoders are on (0 or 1), whose entries
// 4 * line .. 4 * line + 3 Shift turns; values holds each entry's Q31 word, as
// render receives it in parameters[8 + i] (0 beyond the declared entries and for
// an absent one). Read-only and display-only: audio callbacks never call it.
struct ShiftRow {
    u32 held;
    u32 line;
    i32 values[shift_entries];
};
}}

// The Shift row of the synth on a track; never null. The device fills it for the
// selected instrument track's synth; the browser preview from its host
// (PreviewDisplay.tick's shift row); every other track and machine reads zeros.
extern "C" const opxy::draw::ShiftRow* opxy_synth_shift(opxy::u32 track);

namespace opxy { namespace draw {
inline const ShiftRow& shift_row(const Input& input) { return *opxy_synth_shift(input.track); }
}}
