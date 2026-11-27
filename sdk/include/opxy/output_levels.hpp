#pragma once
#include <opxy/encoder_activity.hpp>
namespace opxy { namespace draw {
// Stock's own level meters as a screen with outputLevels reads them each frame
// (docs/display.md#output-levels): the device's output, left and right, and the
// selected input, each the raw signed Q31 word stock's meters move by. Any curve
// that turns them into a meter's position belongs to the machine. available is 1
// when the host reports levels; every level reads 0 otherwise.
struct OutputLevels {
    u32 available;
    i32 output_left, output_right, input;
};
// The input the display would take otherwise, then the levels.
template <class Base> struct WithOutputLevels : Base { OutputLevels levels; };
typedef WithOutputLevels<Input> LevelsInput;
static_assert(sizeof(LevelsInput) == sizeof(Input) + 16 && sizeof(WithOutputLevels<EncoderTelemetryInput>) ==
              sizeof(EncoderTelemetryInput) + 16, "Output level drawing input layout");
}}
