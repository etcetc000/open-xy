#include "input_monitor.hpp"
#include "stock.hpp"
#include <opxy/audio.hpp>
#include <opxy/q31.hpp>

void sdk_input_monitor_audio() {
    using namespace opxy;
    using namespace opxy::platform;
    static input_monitor::Envelope envelope;
    const u32 marker = word(0x84eab39c + 12 * 0xe8 + input_monitor::word_index * 4);
    if (!envelope.begin(marker)) return;
    // Stock has rendered its input to this private interleaved scratch, before
    // adding it to the music or any FX send. The user's levels stay untouched.
    i32* buffer = at<i32>(0x11903600);
    for (u32 frame = 0; frame < block_frames; ++frame) {
        const i32 gain = envelope.next();
        if (gain == 0x7fffffff) continue;
        buffer[frame * 2] = q31::multiply(buffer[frame * 2], gain);
        buffer[frame * 2 + 1] = q31::multiply(buffer[frame * 2 + 1], gain);
    }
}
