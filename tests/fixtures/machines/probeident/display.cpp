#include "display.h"
#include <opxy/device_identity.hpp>

namespace probeident {
inline void display_reset(DisplayState& state) { state.frames = 0; }

// The GUI's copy of the device identity, six bytes a line in hex, under a
// title that says whether it is known yet.
inline void display(DisplayState& state, const opxy::draw::Input&, Frame& frame) {
    using namespace opxy::draw;
    ++state.frames;
    const char digits[] = "0123456789ABCDEF";
    opxy::u8 identity[opxy::device_identity_bytes];
    const bool known = opxy::device_identity(identity, opxy::device_identity_bytes);
    text(frame, 0, 40 * unit, 70 * unit, unit, known ? "IDENTITY" : "WAITING");
    for (opxy::u32 line = 0; line < 3; ++line) {
        char chars[text_capacity] = {};
        for (opxy::u32 i = 0; known && i < 6; ++i) {
            const opxy::u8 value = identity[line * 6 + i];
            chars[i * 3] = digits[value >> 4];
            chars[i * 3 + 1] = digits[value & 15];
            chars[i * 3 + 2] = i < 5 ? ' ' : 0;
        }
        text(frame, 1 + line, 40 * unit, static_cast<opxy::i32>(100 + 30 * line) * unit, unit, chars);
    }
    place(frame, 0, 420 * unit, 70 * unit, unit, known ? 1 : 0, fill_and_stroke);
}
}
