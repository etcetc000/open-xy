/* A synth's Shift row (SDK 1.13): the hook only a build with such a synth links
 * (the synth_shift service). The decision is C++ (synth_controls.cpp).
 */
.section .text
.align 2
.extern sdk_synth_shift_press

/* 8c0b2a5e: the instrument controller's shifted key handler (8c0b28a8, its
 * vtable +16), reached through its jump table for encoder codes 10..13
 * (8400e344..8400e350 -> 8c0b2a56), calls stock's Shift-press action with
 * R0 = track, R1 = code and R2 = the controller + 12, then leaves through
 * `R1 = 0; JUMP.S 8c0b28f6`: eight bytes, which no stock branch enters. The stub
 * passes the three arguments to sdk_synth_shift_press, which makes stock's call
 * first, then sets R1 and continues at 8c0b28f6 as those bytes did. P0 is dead
 * there: the path reads it only after a call. */
.global sdk_synth_shift_press_hook
.type sdk_synth_shift_press_hook, STT_FUNC
sdk_synth_shift_press_hook:
    LINK 0;
    SP += -12;
    CALL sdk_synth_shift_press;
    SP += 12;
    UNLINK;
    R1 = 0;
    P0.H = 0x8c0b;
    P0.L = 0x28f6;
    JUMP (P0);
.size sdk_synth_shift_press_hook, .-sdk_synth_shift_press_hook
