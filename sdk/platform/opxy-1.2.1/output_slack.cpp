// One block of output slack on the DSP board (output_slack.hpp). DSP board, audio task only.
#include "output_slack.hpp"

namespace os = opxy::platform::output_slack;
using opxy::u32;

extern "C" {
// The wrapper the audio task calls once armed (output_slack_hooks.s).
void sdk_slack_block(u32 tx, u32 rx) __asm__("sdk_slack_block");
// Called by the wrapper first: returns the buffer stock's block function renders into.
u32 sdk_slack_prepare(u32 tx, u32 rx) __asm__("sdk_slack_prepare");
// The per-block hook's arming step (dsp_block.cpp).
void sdk_slack_tick() __asm__("sdk_slack_tick");
}

namespace {
// The state (zero-initialised) and the queue: one block, every plane, as stock's block function leaves a
// transmit half.
os::Slack slack;
u32 queue[os::kWords] __attribute__((aligned(32)));

inline volatile u32& word(u32 address) { return *reinterpret_cast<volatile u32*>(address); }
inline volatile u32* half(u32 index) { return reinterpret_cast<volatile u32*>(os::kTxBase + index * os::kHalfBytes); }
inline void ssync() { __asm__ __volatile__("SSYNC;" : : : "memory"); }
}

// 8c0c6ffe's hook, after the whole DSP callback (DSP board): after kArmBlocks blocks, install the wrapper.
void sdk_slack_tick() {
    os::Slack& s = slack;
    if (s.state != os::kIdle) return;
    const bool dsp_board = *reinterpret_cast<const volatile unsigned char*>(os::kRole) == 0;
    if (os::arm(s, dsp_board, word(os::kTaskBlock))) word(os::kTaskBlock) = reinterpret_cast<u32>(&sdk_slack_block);
}

// The audio task's iteration, before stock's block function: `tx` is the half the task will point the DMA at
// when the block returns, `rx` its receive half (passed on unchanged by the wrapper).
u32 sdk_slack_prepare(u32 tx, u32 rx) {
    (void)rx;
    os::Slack& s = slack;
    const u32 i = os::half_of(tx);
    if (i > 1) return tx;
    // Deliver the previous block to this half and point the DMA at it now; render the new block into the queue.
    volatile u32* const halves[2] = {half(0), half(1)};
    if (!os::deliver(s, halves, i, queue, word(os::kUsbMode) == 2)) return tx;   // stock this block
    ssync();
    word(os::kDmaStart) = tx;
    ssync();
    return reinterpret_cast<u32>(queue);
}
