// Main-thread WebAssembly drawing transport. Separate from audio scheduling.
#include "display_binding.hpp"
#include <opxy/sample_overview.hpp>
namespace {
opxy::binding::DisplayState state;
opxy::draw::Input input;
opxy::binding::Frame frame;
// The overview a sample engine's screen reads; the host writes it (empty until then).
opxy::draw::SampleOverview sample;
}
const opxy::draw::SampleOverview* opxy_sample_overview() { return &sample; }
extern "C" {
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
opxy::draw::SampleOverview* opxy_display_sample() { return &sample; }
}
