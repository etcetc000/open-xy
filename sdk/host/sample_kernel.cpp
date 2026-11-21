// The host build of the sample kernel: the reference itself.
#include <opxy/sample_kernel.hpp>

extern "C" void opxy_sample_interpolate(opxy::i32* output, const opxy::u32* positions, opxy::u32 count,
                                        const opxy::SampleTaps* taps) {
    opxy::sample_interpolate_reference(output, positions, count, *taps);
}
extern "C" void opxy_sample_crossfade(opxy::i32* output, const opxy::SampleFade* fade, opxy::u32 count,
                                      const opxy::SampleTaps* taps) {
    opxy::sample_crossfade_reference(output, *fade, count, *taps);
}
