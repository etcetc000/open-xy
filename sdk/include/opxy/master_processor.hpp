// One selectable stereo master processor; independent of its algorithm or UI.
#pragma once
#include <opxy/audio.hpp>
namespace opxy { namespace master {
const u32 frames = block_frames, sample_rate = 44100;
struct Processor {
    const char* label;
    void (*reset)(); // Audio role, bounded/no allocation: discard all retained audio.
    void (*prepare)(Parameters parameters);
    void (*process)(i32* left, i32* right);
};
}} // namespace opxy::master
