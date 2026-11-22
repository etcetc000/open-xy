#pragma once
#include <opxy/audio_transport.hpp>
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
typedef WithEncoderActivity<AudioInput> EncoderAudioInput;
typedef WithEncoderActivity<TelemetryInput> EncoderTelemetryInput;
static_assert(sizeof(EncoderInput) == sizeof(Input) + 8 && sizeof(EncoderAudioInput) == sizeof(AudioInput) + 8 &&
              sizeof(EncoderTelemetryInput) == sizeof(TelemetryInput) + 8, "Encoder drawing input layout");
}}
