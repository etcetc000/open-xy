// One block of output slack on the DSP board (output_slack.cpp, output_slack_hooks.s).
//
// Stock (OS 1.2.1, DSP board): the audio task (8d4d244c) wakes on the transmit DMA's interrupt, toggles its
// buffer index (0x84038a1c), calls the block function through the pointer at 0x11b008cc with one of the two
// transmit halves (0x0803c000 + index * 0x1000) and its receive half, and afterwards writes that half's address
// to 0x11b008d4: the start word of the transmit DMA's only descriptor (0x11b008d0, which links to itself). The
// DMA re-reads that word at every block boundary, so it plays whatever half the task last pointed at. A block
// that ends after the boundary is heard as the previous block again, torn by the next one, and is itself never
// played.
//
// With the slack armed, the task calls sdk_slack_block instead. It first copies the previous block (complete,
// in the queue Q) into the half the task was given and points the DMA at it, then has stock's block function
// render the new block into Q. The listener hears every block one block (64 samples, 1.45 ms) later, and a
// block may now finish up to about two periods after its own interrupt: the copy that delivers it runs at the
// start of the next iteration, which the task starts as soon as the late block ends (the semaphore holds the
// interrupt it missed).
//
// Arming: sdk_dsp_block (the per-block hook) counts kArmBlocks blocks, then installs the wrapper if the task's
// pointer still holds stock's block function. The wrapper runs stock unchanged until the newest block's main
// and USB-record planes (and the USB monitor taps in multichannel mode) are quiet (every sample's magnitude
// below kQuiet); then Q takes that block and the DMA plays it once more (the one inserted block), and the
// queue runs from then on. Nothing disarms it. Stock writes the task's pointer only in its audio init
// (8d4d2548, from 8c0c6c98), which only the two boot sequences call (8c0b95e2, 8c0b9ff6), long before the
// slack arms; and in its fault handler (8d4d27a4, from 8c0cf6b0), which installs a silent block function as
// before. So nothing has to re-arm. (By reading the call sites, not measured: if the audio init re-ran, the
// slack would be silently off and the audio stock.)
//
// The pure parts are here, with no stock addresses touched, and run in a host test (tests/test_output_slack.py):
// the quiet check, the half index, arming (arm) and one wrapper call's delivery (deliver).
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace output_slack {

const u32 kTxBase = 0x0803c000;        // the two transmit halves in L2 (cache attributes not read), 16 planes x 64 words each
const u32 kHalfBytes = 0x1000;
const u32 kWords = kHalfBytes / 4;     // 1024
const u32 kPlaneWords = 64;
const u32 kTaskBlock = 0x11b008cc;     // the audio task's block-function pointer (read every iteration)
const u32 kDmaStart = 0x11b008d4;      // the transmit descriptor's start word
const u32 kStockBlock = 0x8c0c6f8c;    // stock's DSP block function
const u32 kRole = 0x86c05ca4;          // 0 = the DSP board

// About 4 s of blocks (689 a second) before the wrapper is installed.
const u32 kArmBlocks = 2756;
const u32 kUsbMode = 0x84039580;       // the DSP board's USB channel mode (8d4db38c): 2 = multichannel
// Quiet: every word of the checked planes below 2^17 in magnitude (-84 dBFS of a full-scale 32-bit word).
const u32 kQuiet = 1u << 17;
// Planes checked: main left/right (2 x 64 words at 0 and 2), the USB record pair (1, 3), which stock's block
// function writes every block; in multichannel mode also the other odd planes (the USB monitor taps).
const u32 kQuietPlanes = 4;

enum State : u32 { kIdle = 0, kWaiting = 1, kArmed = 2 };

// Zero-initialised (the loader runs no constructors).
struct Slack {
    u32 blocks;        // blocks counted while idle
    volatile u32 state;
};

// The transmit half index of a buffer the task passed, or 2 for anything else.
inline u32 half_of(u32 tx) {
    if (tx == kTxBase) return 0;
    if (tx == kTxBase + kHalfBytes) return 1;
    return 2;
}

inline bool quiet_word(u32 v) {
    const u32 m = (v & 0x80000000u) ? 0u - v : v;
    return m < kQuiet;
}

// Planes 0-3 of a half (and the odd planes 5-15 when `taps`): every sample quiet.
inline bool quiet(const volatile u32* half, bool taps) {
    for (u32 i = 0; i < kQuietPlanes * kPlaneWords; ++i)
        if (!quiet_word(half[i])) return false;
    if (taps)
        for (u32 p = 5; p < 16; p += 2)
            for (u32 i = 0; i < kPlaneWords; ++i)
                if (!quiet_word(half[p * kPlaneWords + i])) return false;
    return true;
}

inline void copy(volatile u32* to, const volatile u32* from) {
    for (u32 i = 0; i < kWords; ++i) to[i] = from[i];
}

// The per-block arming step: true when the wrapper is to be installed now, on the DSP board, after
// kArmBlocks blocks, while the task's pointer still holds stock's block function.
inline bool arm(Slack& s, bool dsp_board, u32 task_block) {
    if (s.state != kIdle || !dsp_board) return false;
    if (++s.blocks < kArmBlocks) return false;
    if (task_block != kStockBlock) return false;   // not stock's block function: leave it alone
    s.state = kWaiting;
    return true;
}

// One wrapper call for transmit half `index` (0 or 1) of `halves`: false while waiting for a quiet block
// (stock renders into the half as before); else the previous block, complete in `queue`, is copied into the
// half, and stock renders the new block into the queue. Arming takes the newest complete block, the one in
// the other half, into the queue first, so it plays twice.
inline bool deliver(Slack& s, volatile u32* const halves[2], u32 index, volatile u32* queue, bool taps) {
    if (s.state == kWaiting) {
        const volatile u32* newest = halves[1 - index];
        if (!quiet(newest, taps)) return false;
        copy(queue, newest);
        s.state = kArmed;
    }
    if (s.state != kArmed) return false;
    copy(halves[index], queue);
    return true;
}
}}}
