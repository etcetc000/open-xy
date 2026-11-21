#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace input_monitor {
// Last LIVE word of external-audio track 12. Never written to stored controls.
const u32 mark = 0x20000000u;
const u32 mask = 0xe0000000u;
const u32 released = 0x60000000u;
const u32 word_index = 57;
const u32 fade_in_frames = 2205;   // 50 ms at 44.1 kHz
const u32 fade_out_frames = 8820;  // 200 ms
const u32 release_us = 205000;    // Fade plus more than two audio blocks.

// Audio-role envelope, separate from the GUI's preference and lease state.
class Envelope {
    u32 token_ = 0, missing_ = 0, remaining_ = 0;
    i32 gain_ = 0, target_ = 0, step_ = 0, remainder_ = 0, error_ = 0, sign_ = 0;
    u32 duration_ = 1;
    void target(i32 value, u32 frames) {
        target_ = value; duration_ = remaining_ = frames; error_ = 0;
        const i32 difference = value - gain_;
        step_ = difference / static_cast<i32>(frames);
        const i32 rest = difference % static_cast<i32>(frames);
        sign_ = rest < 0 ? -1 : 1;
        remainder_ = rest < 0 ? -rest : rest;
    }
public:
    // false: stock/manual monitoring, unity and no extra work on its buffer.
    bool begin(u32 marker) {
        if (marker == released) {
            token_ = missing_ = 0; gain_ = target_ = 0; remaining_ = 0;
            return false;
        }
        if ((marker & mask) != mark) {
            if (token_ && ++missing_ <= 12) return true; // transient live-value reset
            token_ = missing_ = 0; gain_ = target_ = 0; remaining_ = 0;
            return false;
        }
        missing_ = 0;
        const u32 token = marker & ~1u;
        if (token != token_) { token_ = token; gain_ = target_ = 0; remaining_ = 0; }
        const i32 desired = marker & 1u ? 0x7fffffff : 0;
        if (desired != target_) target(desired, desired ? fade_in_frames : fade_out_frames);
        return true;
    }
    i32 next() {
        if (remaining_) {
            gain_ += step_; error_ += remainder_;
            if (error_ >= static_cast<i32>(duration_)) { gain_ += sign_; error_ -= static_cast<i32>(duration_); }
            if (!--remaining_) gain_ = target_;
        }
        return gain_;
    }
};
}}}

extern "C" {
void sdk_input_monitor_startup() __asm__("sdk_input_monitor_startup");
bool sdk_input_monitor_menu() __asm__("sdk_input_monitor_menu");
bool sdk_input_monitor_shift_visible() __asm__("sdk_input_monitor_shift_visible");
opxy::u32 sdk_input_monitor_choice() __asm__("sdk_input_monitor_choice");
bool sdk_input_monitor_turn(opxy::u32, const opxy::i32*) __asm__("sdk_input_monitor_turn");
void sdk_input_monitor_frame(opxy::u32, opxy::u32) __asm__("sdk_input_monitor_frame");
void sdk_input_monitor_audio() __asm__("sdk_input_monitor_audio");
}
