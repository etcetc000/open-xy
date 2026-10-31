// A stereo master processor. Parameters follow the shared normalized Q31
// convention.
#pragma once
#include <opxy/audio.hpp>
namespace opxy {
namespace master {
const u32 frames = block_frames, sample_rate = 44100;
struct Filter {
    const char *label;
    void (*reset)();
    void (*prepare)(Parameters parameters); // Eight slots, undeclared slots zero.
    void (*process)(i32 *left, i32 *right);
};
// Export extensions::<id>::filter. Display uses draw::Frame/Input and the
// normal display_reset/display callbacks, independently of audio and this
// descriptor.
} // namespace master
} // namespace opxy
