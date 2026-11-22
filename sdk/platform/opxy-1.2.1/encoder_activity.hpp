#pragma once
#include <opxy/encoder_activity.hpp>
namespace opxy { namespace platform {
// Pinned XY1.2.1 stock getter. Press/turn dispatch writes the flags before
// controls, and the GUI loop clears them after screen frame callbacks.
// Read only: every active screen receives the same physical frame snapshot.
inline draw::EncoderActivity encoder_activity() {
    typedef u32 (*Getter)(u32, u32);
    const Getter getter = reinterpret_cast<Getter>(0x8d51f1beu);
    draw::EncoderActivity result = {1, 0};
    for (u32 i = 0; i < 4; ++i) if (getter(i, 0)) result.mask |= 1u << i;
    return result;
}
}}
