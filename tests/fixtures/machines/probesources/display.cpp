#include "display.h"

namespace probesources {
inline void display_reset(DisplayState& state) { state.frames = 0; }

// Two dots: one fixed, one turned by the Fold control.
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    ++state.frames;
    place(frame, 0, 200 * unit, 120 * unit, unit, 0, fill_and_stroke);
    place(frame, 1, 280 * unit, 120 * unit, unit, 1, fill_and_stroke);
    turn(frame, 1, sweep(input.slots[0], 1440));
}
}
