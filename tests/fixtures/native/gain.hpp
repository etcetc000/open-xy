#pragma once
#include <opxy/native.hpp>
#include <opxy/types.hpp>

// native/gain.s on the device.
extern "C" void probesources_native_gain(opxy::i32* samples, unsigned count, opxy::i32 gain);

namespace probesources {
// The definition: each sample's high half times gain (0..32767), doubled.
inline void gain_reference(opxy::i32* samples, unsigned count, opxy::i32 gain) {
    for (unsigned i = 0; i < count; ++i) samples[i] = (samples[i] >> 16) * gain * 2;
}

inline void apply_gain(opxy::i32* samples, unsigned count, opxy::i32 gain) {
    OPXY_NATIVE(probesources_native_gain, gain_reference)(samples, count, gain);
}
}
