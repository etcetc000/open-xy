#include "display.h"

namespace probeplayer {
inline void display_reset(DisplayState& display) { display.frames = display.note = 0; }

// A curve whose peak follows the held key, a dot that pulses while an encoder moves,
// and the key's number; it reads the State the player's callbacks keep.
inline void display(DisplayState& display, const State& state, const opxy::player::Controls& controls,
                    const opxy::draw::EncoderInput& input, Frame& frame) {
    using namespace opxy::draw;
    ++display.frames;
    if (state.held) display.note = state.note & 127;
    // Lower on a drum track: Controls.instrument, which every drawn player's unit has (SDK 1.25).
    const opxy::i32 peak = 40 + static_cast<opxy::i32>(display.note) - (controls.value[2] ? 30 : 0) -
                           (controls.instrument == opxy::player::DrumInstrument ? 10 : 0);
    Frame::path_type& curve = frame.paths[0];
    curve.key = 1 + display.note;
    curve.points[0] = {-200 * unit, 0};
    curve.points[1] = {-80 * unit, -peak * unit};
    curve.points[2] = {80 * unit, -peak * unit};
    curve.points[3] = {200 * unit, 0};
    affine(frame, 0, Matrix{1024, 0, 0, 1024, 240 * unit, 170 * unit});
    const opxy::i32 pulse = input.encoders.available && input.encoders.mask ? 2048 : 1024;
    affine(frame, 1, Matrix{pulse, 0, 0, pulse, 240 * unit, (160 - peak) * unit});
    rgba(frame, 0, color(state.held ? 255 : 120, 200, 255));
    rgba(frame, 1, color(255, 255, 255), rgba_fill);
    char digits[4] = {static_cast<char>('0' + display.note / 100), static_cast<char>('0' + display.note / 10 % 10),
                      static_cast<char>('0' + display.note % 10), 0};
    text(frame, 0, 20 * unit, 200 * unit, unit, digits);
}
}
