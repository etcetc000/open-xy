// The DSP board's per-block hook (dsp_block_hooks.s): the output slack's arming
// (output_slack.hpp).
// DSP board, audio context only.
#include <opxy/types.hpp>

extern "C" {
void sdk_dsp_block() __asm__("sdk_dsp_block");
// One block of output slack's arming step (output_slack.cpp).
void sdk_slack_tick() __asm__("sdk_slack_tick");
}

// After 8c0c6ffe, the whole DSP callback for one block (dsp_block_hooks.s).
void sdk_dsp_block() {
    sdk_slack_tick();
}
