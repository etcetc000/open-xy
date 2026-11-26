/* The DSP board's per-block hook (dsp_block.cpp). */
.section .text
.align 2

/* DSP board: 8c0c6ffe, the audio task's CALL.A 11a02864 (R0 the TX buffer, R1
 * TX + 0x400, R2), the whole DSP callback for one 64-sample block: input copy,
 * every track, the mixer, sends and master; then sdk_dsp_block (output slack's
 * arming). A call replaced by a call: the arguments reach stock untouched, its
 * result comes back in R0, and P3, which the task reads after it, is
 * callee-saved. */
.extern sdk_dsp_block
.global sdk_dsp_block_audio
.type sdk_dsp_block_audio, STT_FUNC
sdk_dsp_block_audio:
    LINK 0;
    [--SP] = R7;
    SP += -12;
    P1 = 0x11a02864;
    CALL (P1);
    R7 = R0;
    CALL sdk_dsp_block;
    R0 = R7;
    SP += 12;
    R7 = [SP++];
    UNLINK;
    RTS;
.size sdk_dsp_block_audio, .-sdk_dsp_block_audio
