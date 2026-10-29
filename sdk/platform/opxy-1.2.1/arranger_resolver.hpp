#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {

// Phase conversion without wide division helpers in the sequencer callback.
inline u32 arranger_multiply_modulo(u32 value, u32 factor, u32 period) {
    value %= period;
    u32 result = 0;
    while (factor) {
        if (factor & 1) result = result >= period - value ? result - (period - value) : result + value;
        factor >>= 1;
        if (factor) value = value >= period - value ? value - (period - value) : value + value;
    }
    return result;
}

// Pure project lookup; never changes the scene's pattern references.
inline const u8* arranger_pattern_source(const u8* project, u32 track, u32 pattern, bool instrument) {
    if (!project || track >= 8 || pattern >= 16) return nullptr;
    if (instrument && ((project[12] | (u32(project[13]) << 8)) & (1u << track)))
        pattern = project[14 + track];
    const u8* pool = project + 0xd7c + track * 0x53f44;
    const u8* size = pool + 0x53f40;
    const u32 count = size[0] | (u32(size[1]) << 8) | (u32(size[2]) << 16) | (u32(size[3]) << 24);
    return pattern < 16 && pattern < count && count <= 16 ? pool + pattern * 0x53f4 : nullptr;
}

}}
