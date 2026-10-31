/* 8d5aca48: stock's displayed-value getter 8d5ac9dc(project, track, index, ...)
 * has set R0 nonzero when the sequencer plays, and is about to show the live
 * value then, or else the base value from 8d5ac910. The jump hook replaces
 * that choice: R4 holds the track, R7 the index and [SP + 0x28] the project.
 * The stub calls sdk_param_shown(project, track, index, playing) and resumes
 * at 8d5aca8e, which adds the LFO amount in R6, clamps at R5 (0) and returns.
 * Callee-saved R4-R7 and P3-P5 are all stock still needs.
 */
.section .text
.align 2
.extern sdk_param_shown
.global sdk_param_shown_hook
.type sdk_param_shown_hook, STT_FUNC
sdk_param_shown_hook:
    R3 = R0;
    R0 = [SP + 0x28];
    R1 = R4;
    R2 = R7;
    SP += -16;
    [SP + 12] = R3;
    CALL sdk_param_shown;
    SP += 16;
    P0.L = 0xca8e;
    P0.H = 0x8d5a;
    JUMP (P0);
.size sdk_param_shown_hook, .-sdk_param_shown_hook
