#pragma once
#include <opxy/arranger.hpp>
#include "arranger_resolver.hpp"

namespace opxy { namespace platform {
static inline u32 arranger_source_rate(u32 scale) {
    static const u32 rates[16] = {5040, 3360, 2240, 1680, 1260, 840, 560, 420, 336, 280, 240, 210, 210, 140, 105, 105};
    return scale < 16 ? rates[scale] : 0;
}

// Caller validates nonzero source length and scale <16. Supports revealing
// material before a clip's original left edge without signed time overflow.
static inline u32 arranger_source_phase(const arranger::Clip& clip, const u8* source, u32 time) {
    const u32 period = u32(source[0]) * 480 * 1680;
    u32 phase = arranger_multiply_modulo(clip.offset, 1680, period);
    phase = phase >= period - clip.phase_remainder ? phase - (period - clip.phase_remainder) :
        phase + clip.phase_remainder;
    const u32 elapsed = arranger_multiply_modulo(time >= clip.start ? time - clip.start : clip.start - time,
        arranger_source_rate(source[8]), period);
    if (time < clip.start) return elapsed > phase ? period - (elapsed - phase) : phase - elapsed;
    return elapsed >= period - phase ? elapsed - (period - phase) : elapsed + phase;
}
}}
