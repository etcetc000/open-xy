#pragma once
#include "sample_preview.hpp"

namespace opxy { namespace platform {
// Positive u32 rational to binary32, rounded to nearest/even. Normalizing
// each operand separately keeps every shift and division within one word.
inline u32 slice_ratio_bits(u32 numerator, u32 denominator) {
    if (!numerator || !denominator) return 0;
    i32 exponent = 0;
    while (!(numerator & 0x80000000u)) { numerator <<= 1; --exponent; }
    while (!(denominator & 0x80000000u)) { denominator <<= 1; ++exponent; }
    u32 remainder;
    if (numerator >= denominator) remainder = numerator - denominator;
    else { --exponent; remainder = numerator - (denominator - numerator); }
    u32 significand = 1;
    for (u32 bit = 0; bit < 23; ++bit) {
        significand <<= 1;
        if (remainder >= denominator - remainder) {
            remainder -= denominator - remainder;
            significand |= 1;
        } else remainder <<= 1;
    }
    if (remainder > denominator - remainder ||
        (remainder == denominator - remainder && (significand & 1))) ++significand;
    return (u32(exponent + 126) << 23) + significand;
}
// The slicer supplies a binary32 file rate, known to be an integer Hz value.
inline u32 slice_file_rate(u32 bits) {
    if (bits < 0x45fa0000u || bits > 0x483b8000u) return 0; // 8000..192000
    return ((bits & 0x007fffffu) | 0x00800000u) >> (150u - (bits >> 23));
}
}}

extern "C" {
void sdk_sample_slice_config(opxy::platform::SamplePreviewRange*, opxy::u32, opxy::u32, const void*)
    __asm__("sdk_sample_slice_config");
opxy::u32 sdk_sample_slice_clock_rate(opxy::u32, const opxy::platform::SamplePreviewRange*, opxy::u32)
    __asm__("sdk_sample_slice_clock_rate");
}
