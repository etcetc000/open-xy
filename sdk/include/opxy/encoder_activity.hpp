#pragma once
#include <opxy/audio_transport.hpp>
namespace opxy { namespace draw {
// GUI-frame physical encoder activity. A zero mask is meaningful only when
// available == 1. Never inferred from parameter differences or note gates.
struct EncoderActivity {
    u32 available;
    u32 mask;
    bool valid() const { return available <= 1 && mask < 16 && (available || !mask); }
    bool active(u32 physical) const { return available == 1 && physical < 4 && (mask & (1u << physical)); }
};
template<class Base> struct WithEncoderActivity : Base { EncoderActivity encoders; };
typedef WithEncoderActivity<Input> EncoderInput;
typedef WithEncoderActivity<AudioInput> EncoderAudioInput;
typedef WithEncoderActivity<TelemetryInput> EncoderTelemetryInput;
static_assert(sizeof(EncoderInput) == 92 && sizeof(EncoderAudioInput) == 160 &&
              sizeof(EncoderTelemetryInput) == 1140, "Encoder input ABI");
}}
