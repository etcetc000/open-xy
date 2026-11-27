#include "display.h"
namespace probescope {
inline void display_reset(DisplayState& state) { state.frames = 0; }
// The scope, its peak and the output level as glyphs, and a reset on any encoder touch.
inline void display(DisplayState& state, const opxy::draw::WithOutputLevels<opxy::draw::EncoderTelemetryInput>& input,
                    Frame& frame) {
    using namespace opxy::draw;
    ++state.frames;
    Frame::path_type& path = frame.paths[0];
    path.key = input.audio_sequence ? input.audio_sequence : 1;
    for (opxy::u32 i = 0; i < 32; ++i) {
        const opxy::i32 sample = i && input.audio_count ? opxy::telemetry_lane(input.audio_words[1 + (i - 1) % 31]) : 0;
        path.points[i] = {static_cast<opxy::i32>(i * 15) * unit, (110 - (sample >> 9)) * unit};
    }
    for (opxy::u32 n = 0; n < Frame::node_count; ++n) affine(frame, n, Matrix{1024, 0, 0, 1024, 0, 0});
    glyph(frame, 0, input.audio_count && opxy::telemetry_lane(input.audio_words[0]) > 8192 ? 1 : 0);
    glyph(frame, 1, input.levels.available && input.levels.output_left > 0x20000000 ? 1 : 0);
    if (input.encoders.available && input.encoders.mask) input.request_audio_reset();
    for (opxy::u32 i = 0; i < 4; ++i) header(frame, i, "");
}
}
