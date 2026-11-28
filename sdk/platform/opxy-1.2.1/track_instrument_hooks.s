/* The track instrument's entry (track_instrument.cpp): in a build with the
 * track_instrument service, the compositor's loop advance at 8d5214c2 enters
 * here in place of sdk_screen_advance (screen_hooks.s), which it matches
 * instruction for instruction but for the C++ it calls: sdk_instrument_next
 * publishes each track's kind of instrument at the frame's first slot, then
 * calls sdk_screen_next as sdk_screen_advance does.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_instrument_next

.global sdk_instrument_advance
.type sdk_instrument_advance, STT_FUNC
sdk_instrument_advance:
    [--SP] = P4;
    [--SP] = R7;
    HOOK_SAVE
    R0 = FP;
    R0 += 8;            /* ScreenCursor {R7, P4}, above the saved FP and RETS */
    R1 = [P5];
    CALL sdk_instrument_next;
    HOOK_RESULT R0
    HOOK_RESTORE
    R7 = [SP++];
    P4 = [SP++];
    CC = R0 == 1;
    RTS;
.size sdk_instrument_advance, .-sdk_instrument_advance
