// Main-thread WebAssembly drawing transport. Separate from audio scheduling.
#include "display_binding.hpp"
#include <opxy/sample_display.hpp>
// Browser audio does not yet load samples. Report unavailable data explicitly.
static opxy::u32 preview_activity;
extern "C" bool opxy_sample_display(opxy::u32 track, opxy::SampleDisplay* output) {
    if (output) {
        *output = opxy::SampleDisplay{};
        output->active_notes = track == 0 ? preview_activity : 0;
    }
    return false;
}
namespace {
opxy::binding::DisplayState state;
opxy::draw::Input input;
opxy::binding::Frame frame;
}
extern "C" {
void opxy_display_activity(opxy::u32 notes) { preview_activity = notes <= 128 ? notes : 0; }
opxy::u32 opxy_display_abi() { return 2; }
opxy::u32 opxy_display_state_bytes() { return sizeof(state); }
opxy::u32 opxy_display_input_bytes() { return sizeof(input); }
opxy::u32 opxy_display_frame_bytes() { return sizeof(frame); }
opxy::binding::DisplayState* opxy_display_state() { return &state; }
opxy::draw::Input* opxy_display_input() { return &input; }
opxy::binding::Frame* opxy_display_frame() { return &frame; }
void opxy_display_reset() { opxy::binding::display_reset(&state, &frame); }
void opxy_display_render() { opxy::binding::display_render(&state, &input, &frame); }
opxy::u32 opxy_display_valid() { return opxy::binding::display_valid(&frame); }
}
