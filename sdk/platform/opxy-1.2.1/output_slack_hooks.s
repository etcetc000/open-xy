/* One block of output slack (output_slack.hpp): the block function the DSP board's audio task calls once
 * armed (installed through the task's pointer at 0x11b008cc by sdk_slack_tick; no stock code is patched).
 * The task calls it as it calls stock's 8c0c6f8c: R0 = the transmit half, R1 = the receive half, RETS = the
 * task's loop. sdk_slack_prepare (C) delivers the previous block and returns the buffer to render into; then
 * this jumps to stock's block function with R0 = that buffer and R1 unchanged, so stock returns straight to
 * the task: no frame of ours is on the stack while the block runs. Callee-saved registers are kept by the C
 * call; the task needs nothing else across its call. */
.section .text
.align 2
.extern sdk_slack_prepare
.global sdk_slack_block
.type sdk_slack_block, STT_FUNC
sdk_slack_block:
    [--SP] = RETS;
    [--SP] = R1;
    SP += -12;
    CALL sdk_slack_prepare;
    SP += 12;
    R1 = [SP++];
    RETS = [SP++];
    P1.L = 0x6f8c; P1.H = 0x8c0c;
    JUMP (P1);
.size sdk_slack_block, .-sdk_slack_block
