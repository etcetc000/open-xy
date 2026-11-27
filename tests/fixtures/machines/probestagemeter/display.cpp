#include "display.h"

namespace probestagemeter {
inline void display_reset(DisplayState& state) { state.frames = 0; }

// Two squares sized by the track's left and right peaks, as the track stage published them;
// every 1024th frame asks the stage to clear its held peak.
inline void display(DisplayState& state, const opxy::draw::TelemetryInput& input, Frame& frame) {
    using namespace opxy::draw;
    ++state.frames;
    for (opxy::u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        path.key = 1;
        for (opxy::u32 i = 0; i < Frame::point_count; ++i) {
            const opxy::i32 step = static_cast<opxy::i32>(i % 16) * 4;
            path.points[i].x = (i < 16 ? -32 + step : i < 32 ? 32 : i < 48 ? 32 - step : -32) * unit;
            path.points[i].y = (i < 16 ? -32 : i < 32 ? -32 + step : i < 48 ? 32 : 32 - step) * unit;
        }
        const opxy::i32 peak = input.audio_count ? static_cast<opxy::i32>(input.audio_words[p] >> 22) : 0;
        place(frame, p, static_cast<opxy::i32>(208 + 64 * p) * unit, 120 * unit, unit / 2 + peak, 15, stroke);
    }
    if (input.audio_count && (state.frames & 1023u) == 0) input.request_audio_reset();
}
}
