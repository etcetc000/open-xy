#include "core.h"
#include <opxy/q31.hpp>

namespace probefx {
namespace {
const opxy::i32 kMaximum = opxy::q31::maximum, kMinimum = -kMaximum - 1;

opxy::i32 add(opxy::i32 a, opxy::i32 b) {  // saturating
    if (b > 0 && a > kMaximum - b) return kMaximum;
    if (b < 0 && a < kMinimum - b) return kMinimum;
    return a + b;
}

// Gain from 1 to 8 times, saturating at full scale.
opxy::i32 boost(opxy::i32 sample, opxy::i32 drive) {
    const opxy::i32 step = opxy::q31::multiply(sample, drive);
    opxy::i32 x = sample;
    for (unsigned i = 0; i < 7; ++i) x = add(x, step);
    return x;
}

// Cubic soft clip, 1.5x - 0.5x^3: smooth up to full scale and saturating
// there, since rounding can carry the curve one step past it.
opxy::i32 clip(opxy::i32 x) {
    const opxy::i32 cube = opxy::q31::multiply(opxy::q31::multiply(x, x), x);
    return add(x, -opxy::q31::half(cube - x));
}

// About a 1.5 ms glide toward a new level 0..1, so control changes do not
// click. Both are non-negative, so their difference cannot overflow; the last
// steps move by one so the level arrives exactly.
opxy::i32 smooth(opxy::i32 value, opxy::i32 target) {
    const opxy::i32 difference = target - value, step = difference / 64;
    return value + (step ? step : (difference > 0) - (difference < 0));
}
}

void initialize(State& state) { state.level = 0; }

// Test fixture: a soft-clipping drive on the send. Tone is declared only so
// tests have a control that is not linear from 0 to 1.
void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters parameters) {
    const opxy::i32 drive = parameters[0], level = parameters[1] < 0 ? 0 : parameters[1];
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        state.level = smooth(state.level, level);
        left[i] = opxy::q31::multiply(clip(boost(left[i], drive)), state.level);
        right[i] = opxy::q31::multiply(clip(boost(right[i], drive)), state.level);
    }
}
}
