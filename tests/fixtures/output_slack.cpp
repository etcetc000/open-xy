// Host test of the output slack's pure parts (output_slack.hpp): the quiet
// check, the half index, arming, and one wrapper call's delivery against two
// fake transmit halves and a queue.
#include <cstdio>
#include <cstdlib>
#include "output_slack.hpp"

using namespace opxy;
namespace os = opxy::platform::output_slack;

namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

u32 a[os::kWords], b[os::kWords], q[os::kWords];
volatile u32* const halves[2] = {a, b};

void fill(u32* h, u32 base) { for (u32 i = 0; i < os::kWords; ++i) h[i] = base + i; }
bool holds(const u32* h, u32 base) {
    for (u32 i = 0; i < os::kWords; ++i) if (h[i] != base + i) return false;
    return true;
}
void quiet_block(u32* h) { for (u32 i = 0; i < os::kWords; ++i) h[i] = 0; }
}

int main() {
    // Quiet words: magnitude below 2^17, either sign; 0x80000000 is loud.
    require(os::quiet_word(0) && os::quiet_word((1u << 17) - 1) && os::quiet_word(0u - ((1u << 17) - 1)),
            "a quiet word was loud");
    require(!os::quiet_word(1u << 17) && !os::quiet_word(0u - (1u << 17)) && !os::quiet_word(0x80000000u) &&
            !os::quiet_word(0x7fffffffu), "a loud word was quiet");
    // The checked planes: 0-3 always, the odd planes 5..15 only with the USB monitor taps.
    quiet_block(a);
    a[4 * os::kPlaneWords] = 1u << 20;         // plane 4: never checked
    a[6 * os::kPlaneWords + 3] = 1u << 20;     // plane 6: never checked
    require(os::quiet(a, false) && os::quiet(a, true), "an unchecked plane counted");
    // Each odd plane 5..15, alone and at its last word: a monitor tap, checked with taps only.
    for (u32 p = 5; p < 16; p += 2) {
        a[p * os::kPlaneWords + 63] = 1u << 20;
        require(os::quiet(a, false) && !os::quiet(a, true), "a monitor tap was not checked with taps only");
        a[p * os::kPlaneWords + 63] = 0;
    }
    a[5 * os::kPlaneWords + 63] = 1u << 20;    // plane 5: a monitor tap
    require(os::quiet(a, false) && !os::quiet(a, true), "a monitor tap was not checked with taps only");
    a[3 * os::kPlaneWords + 63] = 0u - (1u << 18);
    require(!os::quiet(a, false), "the last word of plane 3 was not checked");
    // The half index.
    require(os::half_of(os::kTxBase) == 0 && os::half_of(os::kTxBase + os::kHalfBytes) == 1 &&
            os::half_of(os::kTxBase + 4) == 2 && os::half_of(0) == 2, "half_of");

    // Arming: only on the DSP board, after kArmBlocks blocks, only over stock's block function, once.
    {
        os::Slack s = {};
        for (u32 i = 0; i < 10 * os::kArmBlocks; ++i) require(!os::arm(s, false, os::kStockBlock), "armed on the GUI board");
        require(s.state == os::kIdle, "the GUI board left idle");
        os::Slack t = {};
        for (u32 i = 1; i < os::kArmBlocks; ++i) require(!os::arm(t, true, os::kStockBlock), "armed early");
        require(os::arm(t, true, os::kStockBlock) && t.state == os::kWaiting, "not armed after kArmBlocks");
        require(!os::arm(t, true, os::kStockBlock), "armed twice");
        os::Slack u = {};
        for (u32 i = 0; i < 2 * os::kArmBlocks; ++i) require(!os::arm(u, true, 0x8c0c69e4), "armed over another block function");
        require(u.state == os::kIdle, "another block function left idle");
    }

    // Delivery. Idle: stock renders as before.
    os::Slack s = {};
    fill(a, 1000); fill(b, 2000); fill(q, 3000);
    require(!os::deliver(s, halves, 0, q, false) && holds(a, 1000) && holds(b, 2000) && holds(q, 3000),
            "an idle slack touched a buffer");
    // Waiting, the newest block (the other half) loud: stock this block, nothing copied.
    s.state = os::kWaiting;
    b[0] = 1u << 20;
    require(!os::deliver(s, halves, 0, q, false) && s.state == os::kWaiting, "armed on a loud block");
    require(holds(q, 3000) && a[0] == 1000, "a loud block was copied");
    // Waiting, the newest block quiet: the queue takes it from the other half (not this one), and this
    // half gets it too, so it plays twice; then stock renders into the queue.
    quiet_block(b);
    b[7] = 5;
    require(os::deliver(s, halves, 0, q, false) && s.state == os::kArmed, "not armed on a quiet block");
    require(q[7] == 5 && a[7] == 5 && q[8] == 0 && a[8] == 0, "arming did not take the newest block from the other half");
    require(b[7] == 5, "arming wrote the half the DMA plays");
    // Armed: each call copies the queue (the previous block) into the half it was given, and leaves the other.
    fill(q, 7000);
    require(os::deliver(s, halves, 1, q, false) && holds(b, 7000) && holds(q, 7000), "the queue was not delivered");
    require(a[7] == 5, "delivery wrote the other half");
    fill(q, 8000);
    b[0] = 1u << 30;   // loud blocks change nothing once armed
    require(os::deliver(s, halves, 0, q, true) && holds(a, 8000) && b[0] == 1u << 30, "delivery while loud");
    std::puts("ok");
    return 0;
}
