/* Copies of stock's LFO (multi_lfo_audio.hpp): the stubs at stock's LFO
 * sites. Each replaces exactly the eight bytes the profile names and saves
 * every register the C code may clobber (hook.inc).
 *
 * The voice renderers test stock's LFO-on byte before each of their two LFO
 * calls and skip both calls when it is off; with stock's LFO on, a pass-0
 * result also skips pass 1. The gate stubs replace that test: both passes
 * always run (copies may use either), and the C code calls stock's LFO only
 * when it is on. Pass 0 reports nothing; pass 1 reports whether the voice
 * needs its restore, where stock kept the pass's result.
 */
.section .text
.include "sdk/platform/opxy-1.2.1/hook.inc"
.align 2

.extern sdk_multi_lfo_voice0
.extern sdk_multi_lfo_voice1
.extern sdk_multi_lfo_track2
.extern sdk_multi_lfo_track3
.extern sdk_multi_lfo_track4

/* RETS += \bytes, keeping R0 and ASTAT. */
.macro SKIP bytes
    [--SP] = ASTAT;
    [--SP] = R0;
    R0 = RETS;
    R0 += \bytes;
    RETS = R0;
    R0 = [SP++];
    ASTAT = [SP++];
.endm

/* 11a0953e (L1 renderer 11a09308): R7 = 1; R0 = B[P1]; CC = R0 == 0;
 * IF CC JUMP 11a09548. The voice is R6, its track R1, its words R2. Return
 * past the following JUMP.S to the pass-0 call block, to 11a09548 (R7 = 0):
 * pass 1 always follows.
 */
.global sdk_multi_lfo_a_pass0
.type sdk_multi_lfo_a_pass0, STT_FUNC
sdk_multi_lfo_a_pass0:
    HOOK_SAVE
    R0 = 0x98;
    R0 = R6 + R0;
    CALL sdk_multi_lfo_voice0;
    HOOK_RESTORE
    SKIP 2
    RTS;
.size sdk_multi_lfo_a_pass0, .-sdk_multi_lfo_a_pass0

/* 11a095f6: R3 = 0; R0 = B[P1]; CC = R0 == 0; IF CC JUMP 11a09600. Voice R6,
 * track R1, words R2. R3 becomes the restore flag; return past the JUMP.S to
 * the pass-1 call block, to 11a09600 ([FP - 0x30] = R3).
 */
.global sdk_multi_lfo_a_pass1
.type sdk_multi_lfo_a_pass1, STT_FUNC
sdk_multi_lfo_a_pass1:
    HOOK_SAVE
    R0 = 0x98;
    R0 = R6 + R0;
    CALL sdk_multi_lfo_voice1;
    HOOK_RESULT_R3 R0
    HOOK_RESTORE
    SKIP 2
    RTS;
.size sdk_multi_lfo_a_pass1, .-sdk_multi_lfo_a_pass1

/* 8d51b3d8 (renderer 8d51b274): R0 = B[P1]; CC = R0 == 0; IF CC JUMP
 * 8d51b3e0; JUMP.S 8d51bb70 (the pass-0 call block). The voice is R5, its
 * track R1, its words R2; R4 (the pass-0 result) stays 0, so pass 1 follows.
 */
.global sdk_multi_lfo_b_pass0
.type sdk_multi_lfo_b_pass0, STT_FUNC
sdk_multi_lfo_b_pass0:
    HOOK_SAVE
    R0 = 0x98;
    R0 = R5 + R0;
    CALL sdk_multi_lfo_voice0;
    HOOK_RESTORE
    RTS;
.size sdk_multi_lfo_b_pass0, .-sdk_multi_lfo_b_pass0

/* 8d51b520: R0 = B[P1]; CC = R0 == 0; IF CC JUMP 8d51b528; JUMP.S 8d51bb4c.
 * Voice R5, track R1, words R2. R3 becomes the restore flag, which
 * 8d51b528 stores ([SP + 0x3c] = R3).
 */
.global sdk_multi_lfo_b_pass1
.type sdk_multi_lfo_b_pass1, STT_FUNC
sdk_multi_lfo_b_pass1:
    HOOK_SAVE
    R0 = 0x98;
    R0 = R5 + R0;
    CALL sdk_multi_lfo_voice1;
    HOOK_RESULT_R3 R0
    HOOK_RESTORE
    RTS;
.size sdk_multi_lfo_b_pass1, .-sdk_multi_lfo_b_pass1

/* The audio loop 8d510760, one track per iteration: its LFO state at
 * [FP - 0x60], its track at [FP + 8] and its words at [FP + 0xc]. A pass's
 * result goes where stock's pass block stores it, for the restore it gates.
 * HOOK_SAVE links: the loop's FP is then at [FP].
 */
.macro TRACK_PASS function, flag
    HOOK_SAVE
    P0 = [FP];
    R0 = [P0 - 0x60];
    R1 = [P0 + 8];
    R2 = [P0 + 0xc];
    CALL \function;
    P0 = [FP];
    P1 = \flag;
    P0 -= P1;
    [P0] = R0;
    HOOK_RESTORE
.endm

/* 8d510aac: CC = R0 == 0; IF CC JUMP 8d510ab4; JUMP.L 8d5124a8 (pass 2,
 * before the voices). Its result is [FP - 0x4c]; return to 8d510ab4.
 */
.global sdk_multi_lfo_loop_pass2
.type sdk_multi_lfo_loop_pass2, STT_FUNC
sdk_multi_lfo_loop_pass2:
    TRACK_PASS sdk_multi_lfo_track2, 0x4c
    RTS;
.size sdk_multi_lfo_loop_pass2, .-sdk_multi_lfo_loop_pass2

/* 8d510c02: CC = R1 == 0; IF CC JUMP 8d510c10; JUMP.L 8d512482 (pass 4,
 * after the voices). Its result is [FP - 0x64]; return to the NOPs before
 * 8d510c10.
 */
.global sdk_multi_lfo_loop_pass4
.type sdk_multi_lfo_loop_pass4, STT_FUNC
sdk_multi_lfo_loop_pass4:
    TRACK_PASS sdk_multi_lfo_track4, 0x64
    RTS;
.size sdk_multi_lfo_loop_pass4, .-sdk_multi_lfo_loop_pass4

/* 8d512404, the selected track's fallback when nothing else ran: IF !CC
 * JUMP 8d51240c (the pass-3 call block); [FP - 0x4c] = R3; JUMP.L 8d510ca0.
 * Its result is [FP - 0x4c]; both of stock's paths continue at 8d510ca0.
 */
.global sdk_multi_lfo_loop_pass3
.type sdk_multi_lfo_loop_pass3, STT_FUNC
sdk_multi_lfo_loop_pass3:
    TRACK_PASS sdk_multi_lfo_track3, 0x4c
    [--SP] = R0;
    R0.H = 0x8d51;
    R0.L = 0x0ca0;
    RETS = R0;
    R0 = [SP++];
    RTS;
.size sdk_multi_lfo_loop_pass3, .-sdk_multi_lfo_loop_pass3
