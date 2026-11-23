#include "display.h"
namespace probemeter {
inline void display_reset(DisplayState& state) { state.serial = 0; }
// The send's return, the device's output level and a touched encoder bend the curves.
inline void display(DisplayState& state, const opxy::draw::WithOutputLevels<opxy::draw::EncoderAudioInput>& input,
                    Frame& frame) {
    using namespace opxy::draw;
    if (++state.serial == 0) ++state.serial;
    const opxy::i32 audio = input.audio_count ? static_cast<opxy::i32>(input.audio_words[0]) >> 26 : 0;
    const opxy::i32 bend = audio + (input.levels.output_left >> 27) + (input.encoders.mask ? 8 : 0);
    for (opxy::u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        path.key = state.serial;
        path.count = 2 + (state.serial + p) % (Frame::command_capacity - 1);
        for (opxy::u32 i = 0; i < path.count; ++i) {
            const opxy::i32 x = static_cast<opxy::i32>(i * 24) * unit, y = static_cast<opxy::i32>(80 + p * 60) * unit;
            path.commands[i] = {i == 0 ? move_to : i == path.count - 1 ? line_to : quadratic_to,
                                {x - 12 * unit, y - bend * unit}, {x, y}};
        }
        affine(frame, p, Matrix{1024, 0, 0, 1024, 0, 0});
    }
    for (opxy::u32 i = 0; i < 4; ++i) header(frame, i, "");
}
}
