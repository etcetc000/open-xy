/* Stock OS 1.2.1 code, relocated byte for byte, and the glue that runs it. */

/* 8d510f04..8d511014 at 0x0804c000: stock's end of the monitor tap's stage timer
 * (slot 17, P4 its +8), the bookkeeping the profiling-off hook at 8d510f04 skips,
 * falling through to stock's five mixes. */
.section .tapwin, "ax"
.global tapwin
tapwin:
    .incbin "tap.bin"
    JUMP.L mixwin;

/* 8d511014..8d511182 at 0x08048000: the audio loop's five bus mixes, then back. */
.section .mixwin, "ax"
.global mixwin
mixwin:
    .incbin "mix.bin"
    JUMP.L mix_return;

.section .bss
.align 4
save_area:
    .space 16

.section .text
.align 2
/* run_mix(Ctx* c, which): R1, R5, R6, R7, FP and SP from c, R4 = 0, P3 the
 * track buffer's second word, P1 = P2 = [FP - 0x7c] (stock's state at
 * 8d511014); which 0 runs stock's bytes, 1 the SDK hook. which 2 enters at
 * 8d510f04 instead (P3 the buffer's first word, P4 slot 17's +8 at 08035230)
 * and runs stock's tap timer end, then stock's mixes; 3 is the profiling-off
 * hook at 8d510f04: R0-R3, P0-P3 and I0 hold junk (whatever stock left there),
 * straight to sdk_track_mix. Stores P3 and the cycles spent back into c. */
.global _run_mix
.type _run_mix, STT_FUNC
_run_mix:
    [--SP] = (R7:4, P5:3);
    [--SP] = FP;
    [--SP] = RETS;
    P0.L = save_area; P0.H = save_area;
    [P0] = SP;
    [P0 + 4] = R0;
    R2 = R1;
    P1 = R0;
    R1 = [P1];
    R5 = [P1 + 4];
    R6 = [P1 + 8];
    R7 = [P1 + 12];
    FP = [P1 + 20];
    R4 = 0;
    P3.L = 0x3604; P3.H = 0x1190;
    P2 = [FP - 0x7c];
    SP = [P1 + 24];
    P1 = P2;
    CC = R2 == 0;
    IF CC JUMP .Lstock;
    R0 = 2;
    CC = R2 == R0;
    IF CC JUMP .Ltap;
    R0 = 3;
    CC = R2 == R0;
    IF CC JUMP .Lbypass;
    R3 = CYCLES;
    [P0 + 8] = R3;
    JUMP.L sdk_track_mix;
.Lstock:
    R3 = CYCLES;
    [P0 + 8] = R3;
    JUMP.L mixwin;
.Ltap:
    P3.L = 0x3600; P3.H = 0x1190;
    P4.L = 0x5230; P4.H = 0x0803;
    R3 = CYCLES;
    [P0 + 8] = R3;
    JUMP.L tapwin;
.Lbypass:
    R0.L = 0x5a5a; R0.H = 0xa5a5;
    R1 = ~R0;
    R2 = R0 + R1;
    P1 = R0;
    P2 = R1;
    P3 = R2;
    I0 = R0;
    R3 = CYCLES;
    [P0 + 8] = R3;
    P0 = R1;
    JUMP.L sdk_track_mix;
.global mix_return
mix_return:
    R3 = CYCLES;
    P0.L = save_area; P0.H = save_area;
    R2 = [P0 + 8];
    R3 = R3 - R2;
    SP = [P0];
    P1 = [P0 + 4];
    [P1 + 16] = P3;
    [P1 + 28] = R3;
    RETS = [SP++];
    FP = [SP++];
    (R7:4, P5:3) = [SP++];
    RTS;
.size _run_mix, .-_run_mix
