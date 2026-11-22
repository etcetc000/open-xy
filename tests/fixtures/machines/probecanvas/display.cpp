#include "display.h"
#include <opxy/control_interaction.hpp>
namespace probecanvas {
inline void display_reset(DisplayState& state) { state.serial = 0; }
inline void display(DisplayState& state, const opxy::draw::EncoderInput& input, Frame& frame) {
    using namespace opxy::draw;
    if (++state.serial == 0) ++state.serial;
    const opxy::control_interaction::Event touched = opxy::control_interaction::latest(input.track);
    const opxy::u32 moving = input.encoders.available ? input.encoders.mask : 0;
    for (opxy::u32 p = 0; p < Frame::path_count; ++p) {
        Frame::path_type& path = frame.paths[p];
        path.key = state.serial;
        path.count = 2 + (state.serial + p * 13) % (Frame::point_count - 1);
        for (opxy::u32 i = 0; i < path.count; ++i)
            path.points[i] = {static_cast<opxy::i32>(i * 6) * unit, static_cast<opxy::i32>(40 + p * 50) * unit};
        const opxy::i32 lift = (moving >> p & 1) ? -8 * unit : 0;
        affine(frame, p, Matrix{1024, 0, 0, 1024, 0, lift});
        rgba(frame, p, color(touched.available && touched.parameter_index == p ? 255 : 90, 160, 220, 255));
    }
}
}
