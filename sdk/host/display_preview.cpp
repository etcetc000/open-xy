// Main-thread WebAssembly drawing transport. Separate from audio scheduling.
#include "display_binding.hpp"
#include <opxy/control_interaction.hpp>
namespace {
opxy::binding::DisplayState state;
opxy::binding::DisplayInput input;
opxy::binding::Frame frame;
}
extern "C" {
opxy::u32 opxy_control_source(opxy::u32 available) {
    return available<=1 && opxy::control_interaction::configure_preview_source(available!=0);
}
opxy::u32 opxy_control_event(opxy::u32 track,opxy::u32 physical,opxy::u32 parameter,opxy::i32 value) {
    return opxy::control_interaction::submit_preview(track,physical,parameter,value);
}

opxy::u32 opxy_display_abi() { return opxy::binding::display_abi; }
opxy::u32 opxy_display_audio_words() { return opxy::binding::audio_words; }
opxy::u32 opxy_display_state_bytes() { return sizeof(state); }
opxy::u32 opxy_display_input_bytes() { return sizeof(input); }
opxy::u32 opxy_display_frame_bytes() { return sizeof(frame); }
opxy::binding::DisplayState* opxy_display_state() { return &state; }
opxy::binding::DisplayInput* opxy_display_input() { return &input; }
opxy::binding::Frame* opxy_display_frame() { return &frame; }
void opxy_display_reset() { opxy::binding::display_reset(&state, &frame); }
void opxy_display_render() { opxy::binding::display_render(&state, &input, &frame); }
opxy::u32 opxy_display_valid() { return opxy::binding::display_valid(&frame); }
}
