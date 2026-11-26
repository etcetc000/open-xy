#pragma once
#include <opxy/draw.hpp>
// Members stay inline in every object: SDK objects keep no COMDAT copies (tooling.native).
#ifndef OPXY_ALWAYS_INLINE
#define OPXY_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif
namespace opxy { namespace draw {
// GUI-frame physical encoder activity. A zero mask is meaningful only when
// available == 1. Never inferred from parameter differences or note gates.
struct EncoderActivity {
    u32 available;
    u32 mask;
    OPXY_ALWAYS_INLINE bool valid() const { return available <= 1 && mask < 16 && (available || !mask); }
    OPXY_ALWAYS_INLINE bool active(u32 physical) const { return available == 1 && physical < 4 && (mask & (1u << physical)); }
};
template<class Base> struct WithEncoderActivity : Base { EncoderActivity encoders; };
typedef WithEncoderActivity<Input> EncoderInput;
static_assert(sizeof(EncoderInput) == sizeof(Input) + 8, "Encoder drawing input layout");
}}
