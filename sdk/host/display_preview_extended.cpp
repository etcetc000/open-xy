// Main-thread WebAssembly drawing transport for an extended display
// (docs/display.md#extended-drawing): its input type and what that input appends
// to draw::Input. Plain displays use display_preview.cpp.
#include "display_binding.hpp"
#include <opxy/control_interaction.hpp>
#include <opxy/sample_overview.hpp>
#include <opxy/synth_shift.hpp>
namespace {
opxy::binding::DisplayState state;
opxy::binding::DisplayInput input;
opxy::binding::Frame frame;
opxy::draw::SampleOverview sample = {0, 0, 0, 0, 0, 0, 0, 0, 0, {}, {}, opxy::draw::gain_unavailable};
opxy::draw::ShiftRow shift_row = {0, 0, {}};
}
const opxy::draw::SampleOverview* opxy_sample_overview() { return &sample; }
const opxy::draw::ShiftRow* opxy_synth_shift(opxy::u32) { return &shift_row; }
extern "C" {
opxy::u32 sdk_control_copies(opxy::u32) { return 1; }
opxy::i32 sdk_control_display(opxy::u32) { return 0; }
opxy::u32 opxy_display_abi() { return 6; }
// Appended to draw::Input: 4 the encoders' activity (adapters.display_extension).
opxy::u32 opxy_display_extension() { return opxy::binding::display_extension; }
opxy::u32 opxy_display_state_bytes() { return sizeof(state); }
opxy::u32 opxy_display_input_bytes() { return sizeof(input); }
opxy::u32 opxy_display_frame_bytes() { return sizeof(frame); }
opxy::binding::DisplayState* opxy_display_state() { return &state; }
opxy::binding::DisplayInput* opxy_display_input() { return &input; }
opxy::binding::Frame* opxy_display_frame() { return &frame; }
void opxy_display_reset() { opxy::binding::display_reset(&state, &frame); }
void opxy_display_render() { opxy::binding::display_render(&state, &input, &frame); }
opxy::u32 opxy_display_valid() { return opxy::binding::display_valid(&frame); }
opxy::draw::SampleOverview* opxy_display_sample() { return &sample; }
opxy::draw::ShiftRow* opxy_display_shift() { return &shift_row; }
// The browser host's physical gestures (opxy/control_interaction.hpp).
opxy::u32 opxy_control_source(opxy::u32 available) {
    return available <= 1 && opxy::control_interaction::configure_preview_source(available != 0);
}
opxy::u32 opxy_control_event(opxy::u32 track, opxy::u32 physical, opxy::u32 parameter, opxy::i32 value) {
    return opxy::control_interaction::submit_preview(track, physical, parameter, value);
}
}
