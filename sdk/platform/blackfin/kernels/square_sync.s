/* opxy::kernels::square_sync on the device: sdk/include/opxy/kernels/square_sync.hpp
 * holds the definition and the portable reference this matches sample for sample.
 *
 * void opxy_square_sync(SquareSync* s, i16* out, unsigned size), size 1..24,
 * every increment over the call in 2^16..2^31 - 1 (the header checks before
 * calling).
 *
 * Three passes over the chunk, each a zero-overhead hardware loop: the master
 * (samples and wrap reports into the frame), the slave (samples into the
 * frame), and the crossfade into out. In each oscillator loop a sample with no
 * edge and no report takes 9 (master) or 11 (slave) cycles; edges, wraps and
 * resets branch to blocks inside the loop that rejoin its last two
 * instructions. The crossfade takes 4 cycles a sample at a fixed balance and 9
 * while it moves.
 * Exactness:
 *   - With every increment below 2^31 a wrap leaves the phase below 2^31, so a
 *     wrap is never also a rise and a sample has at most one own edge.
 *   - An edge time is the C quotient held below 65536: 65535 when the
 *     numerator's top half reaches the divisor (the quotient would reach
 *     65536), else DIVS and 16 DIVQ, exact for a divisor below 32768.
 *   - A wrap report is phase / (increment >> 7) + 1, the quotient at most
 *     128. A divisor below 32768 takes DIVQ directly; a wider one d is
 *     shifted right by k to below 2^14 and increased by one, which with the
 *     phase shifted by k gives a quotient low by at most one (the relative
 *     error is below 130 / 2^13); one residual comparison corrects it.
 *   - Each oscillator carries next - 16384 and its high state as 0 or -1: the
 *     plain next sample is then (phase >>> 31) ^ -16384, and a sample is the
 *     carried value << 1 kept to 16 bits, (this - 16384) * 2 as in C.
 *   - Polynomial steps: t * t and (65535 - t)^2 are exact 16 x 16 unsigned
 *     products (FU), shifted by 18.
 *   - The crossfade (a * (65535 - w) + b * w) >> 16 is one mixed-mode MAC pair
 *     truncated to its high half; >> 2 and * 3 follow as in C.
 * Clobbers R0..R3, P0..P2, I0, I1, A1, LC0/LT0/LB0, ASTAT.
 */
/* Struct offsets (SquareSync): master 0 phase, 4 increment, 8 step, 12 next,
 * 16 high; slave 20..36 the same; 40 balance start, 44 balance delta,
 * 48 crossfade step.
 * Frame: [SP] a spill word, [SP+4] 24 wrap reports, [SP+28] 25 master samples,
 * [SP+80] 25 slave samples (each with a spare entry the crossfade loop reads
 * past the end). Oscillator loop registers: R0 phase, R1 increment, R2 step,
 * R3 carried next - 16384 (this sample's value until stored), R4 high (0 or
 * -1), R5 and R6 scratch, R7 = -16384, P5 the samples, P4 the reports. */

    .section .text
    .align 4
    .global _opxy_square_sync
    .type _opxy_square_sync, STT_FUNC
_opxy_square_sync:
    [--SP] = (R7:4, P5:3);
    [--SP] = R1;
    SP += -60;
    SP += -60;
    SP += -16;
    P2 = R0;
    P1 = R2;
    M3 = R2;
    R0 = 0;
    [SP + 4] = R0;
    [SP + 8] = R0;
    [SP + 12] = R0;
    [SP + 16] = R0;
    [SP + 20] = R0;
    [SP + 24] = R0;
    R7 = -16384 (X);
    /* ---- master ---- */
    R0 = [P2 + 0];
    R1 = [P2 + 4];
    R2 = [P2 + 8];
    R3 = [P2 + 12];
    R3 = R3 + R7;
    R4 = [P2 + 16];
    R4 = -R4;
    P5 = SP;
    P5 += 28;
    P3 = SP;
    P3 += 4;
    P3 = P3 + P1;
    LSETUP (.Lm_top, .Lm_end) LC0 = P1;
.Lm_top:
    R1 = R1 + R2 (NS) || W [P5 ++] = R5;
    R0 = R0 + R1 (NS);
    CC = R0 < R1 (IU);
    IF CC JUMP .Lm_wrap;
    R6 = R0 >>> 31;
    CC = R6 < R4;
    IF !CC JUMP .Lm_out (BP);
    R5 = R0;
    BITCLR (R5, 31);
    R6 = R1 >> 16;
    R4 = R5 >> 16;
    CC = R4 < R6 (IU);
    IF CC JUMP .Ldiv1;
    R5 = 65535 (Z);
    JUMP .Ledged2;
.Ldiv1:
    DIVS (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    R5 = R5.L (Z);
.Ledged2:
    R6 = R5.L * R5.L (FU);
    R6 >>= 18;
    R5 = ~R5;
    R5 = R5.L (Z);
    R5 = R5.L * R5.L (FU);
    R5 >>= 18;
    R3 = R3 + R6;
    R6 = ~R7;
    R6 = R6 - R5;
    R6 = R6 ^ R7;
    R4 = -1;
    JUMP .Lm_out;
.Lm_wrap:
    /* the report: phase / (increment >> 7) + 1 */
    R5 = R1 >> 7;
    R6 = R0;
    R2 = R5 >> 15;
    CC = R2 == 0;
    IF !CC JUMP .Lm_wide;
    DIVS (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    R6 = R6.L (Z);
    JUMP .Lm_report;
.Lm_wide:
    R2.L = SIGNBITS R5;
    R2 = R2.L (Z);
    R2 = -R2;
    R2 += 17;
    R6 >>= R2;
    R5 >>= R2;
    R5 += 1;
    DIVS (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5); DIVQ (R6, R5);
    R6 = R6.L (Z);
    R5 = R1 >> 7;
    R2 = R6;
    R2 *= R5;
    R2 = R0 - R2;
    CC = R5 <= R2 (IU);
    R2 = CC;
    R6 = R6 + R2;
.Lm_report:
    R6 += 1;
    R5 = LC0;
    P1 = R5;
    P0 = P3;
    P0 -= P1;
    B [P0] = R6;
    R2 = [P2 + 8];
    CC = R4 == 0;
    R6 = 0;
    IF CC JUMP .Lm_out;
    R5 = R0;
    R6 = R1 >> 16;
    R4 = R5 >> 16;
    CC = R4 < R6 (IU);
    IF CC JUMP .Ldiv3;
    R5 = 65535 (Z);
    JUMP .Ledged4;
.Ldiv3:
    DIVS (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    R5 = R5.L (Z);
.Ledged4:
    R6 = R5.L * R5.L (FU);
    R6 >>= 18;
    R5 = ~R5;
    R5 = R5.L (Z);
    R5 = R5.L * R5.L (FU);
    R5 >>= 18;
    R3 = R3 - R6;
    R6 = R7 + R5;
    R6 = R6 ^ R7;
    R4 = 0;
    JUMP .Lm_out;
.Lm_out:
    R5 = R3 << 1;
.Lm_end:
    R3 = R6 ^ R7;
    W [P5 ++] = R5;
    [P2 + 0] = R0;
    [P2 + 4] = R1;
    R3 = R3 - R7;
    [P2 + 12] = R3;
    R4 = -R4;
    [P2 + 16] = R4;
    /* ---- slave ---- */
    R0 = [P2 + 20];
    R1 = [P2 + 24];
    R2 = [P2 + 28];
    R3 = [P2 + 32];
    R3 = R3 + R7;
    R4 = [P2 + 36];
    R4 = -R4;
    P5 = SP;
    P5 += 40;
    P5 += 40;
    P4 = SP;
    P4 += 4;
    P1 = M3;
    LSETUP (.Ls_top, .Ls_end) LC0 = P1;
.Ls_top:
    R1 = R1 + R2 (NS) || R6 = B [P4 ++] (Z);
    CC = R6 == 0;
    IF !CC JUMP .Ls_sync;
    R0 = R0 + R1 (NS) || W [P5 ++] = R5;
    CC = R0 < R1 (IU);
    IF CC JUMP .Ls_wrap;
    R6 = R0 >>> 31;
    CC = R6 < R4;
    IF !CC JUMP .Ls_out (BP);
    R5 = R0;
    BITCLR (R5, 31);
    R6 = R1 >> 16;
    R4 = R5 >> 16;
    CC = R4 < R6 (IU);
    IF CC JUMP .Ldiv5;
    R5 = 65535 (Z);
    JUMP .Ledged6;
.Ldiv5:
    DIVS (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    R5 = R5.L (Z);
.Ledged6:
    R6 = R5.L * R5.L (FU);
    R6 >>= 18;
    R5 = ~R5;
    R5 = R5.L (Z);
    R5 = R5.L * R5.L (FU);
    R5 >>= 18;
    R3 = R3 + R6;
    R6 = ~R7;
    R6 = R6 - R5;
    R6 = R6 ^ R7;
    R4 = -1;
    JUMP .Ls_out;
.Ls_wrap:
    CC = R4 == 0;
    R6 = 0;
    IF CC JUMP .Ls_out;
    R5 = R0;
    R6 = R1 >> 16;
    R4 = R5 >> 16;
    CC = R4 < R6 (IU);
    IF CC JUMP .Ldiv7;
    R5 = 65535 (Z);
    JUMP .Ledged8;
.Ldiv7:
    DIVS (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    R5 = R5.L (Z);
.Ledged8:
    R6 = R5.L * R5.L (FU);
    R6 >>= 18;
    R5 = ~R5;
    R5 = R5.L (Z);
    R5 = R5.L * R5.L (FU);
    R5 >>= 18;
    R3 = R3 - R6;
    R6 = R7 + R5;
    R6 = R6 ^ R7;
    R4 = 0;
    JUMP .Ls_out;
.Ls_sync:
    W [P5 ++] = R5;
    /* reset time rt = (report - 1) << 9, kept in the spill word */
    R5 = R6;
    R5 += -1;
    R5 <<= 9;
    [SP] = R5;
    /* at_reset = phase + (65535 - rt) * (increment >> 16) */
    R2 = R1 >> 16;
    R7 = 65535 (Z);
    R7 = R7 - R5;
    R7 *= R2;
    R7 = R7 + R0;
    /* through = at_reset < phase || (!high && at_reset >= 2^31): R6 */
    CC = R7 < R0 (IU);
    R6 = CC;
    R2 = R7 >>> 31;
    R7 = ~R4;
    R7 = R7 & R2;
    R6 = R6 | R7;
    /* at_reset >= 2^31: this -= rise step of rt, next += its fall step (R7 accumulates next) */
    R7 = 0;
    CC = R2 == 0;
    IF CC JUMP .Ls_noreset;
    R7 = 65535 (Z);
    R7 = MIN (R5, R7);
    R2 = R7.L * R7.L (FU);
    R2 >>= 18;
    R7 = ~R7;
    R7 = R7.L (Z);
    R7 = R7.L * R7.L (FU);
    R7 >>= 18;
    R3 = R3 - R2;
.Ls_noreset:
    R0 = R0 + R1;
    CC = R6 == 0;
    IF CC JUMP .Ls_rephase;
    R6 = R0 >>> 31;
    CC = R6 < R4;
    IF !CC JUMP .Ls_nofirst;
    R5 = R0;
    BITCLR (R5, 31);
    R6 = R1 >> 16;
    R2 = R5 >> 16;
    CC = R2 < R6 (IU);
    IF CC JUMP .Ldiv9;
    R5 = 65535 (Z);
    JUMP .Ledged10;
.Ldiv9:
    DIVS (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    R5 = R5.L (Z);
.Ledged10:
    R6 = R5.L * R5.L (FU);
    R6 >>= 18;
    R5 = ~R5;
    R5 = R5.L (Z);
    R5 = R5.L * R5.L (FU);
    R5 >>= 18;
    R3 = R3 + R6;
    R7 = R7 - R5;
    R4 = -1;
.Ls_nofirst:
    CC = R0 < R1 (IU);
    IF !CC JUMP .Ls_rephase;
    CC = R4 == 0;
    IF CC JUMP .Ls_rephase;
    R5 = R0;
    R6 = R1 >> 16;
    R2 = R5 >> 16;
    CC = R2 < R6 (IU);
    IF CC JUMP .Ldiv11;
    R5 = 65535 (Z);
    JUMP .Ledged12;
.Ldiv11:
    DIVS (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6); DIVQ (R5, R6);
    R5 = R5.L (Z);
.Ledged12:
    R6 = R5.L * R5.L (FU);
    R6 >>= 18;
    R5 = ~R5;
    R5 = R5.L (Z);
    R5 = R5.L * R5.L (FU);
    R5 >>= 18;
    R3 = R3 - R6;
    R7 = R7 + R5;
    R4 = 0;
.Ls_rephase:
    /* phase = rt * (increment >> 16); next += ((phase >>> 31) ^ -16384) + 16384 */
    R5 = [SP];
    R2 = R1 >> 16;
    R5 *= R2;
    R0 = R5;
    R4 = 0;
    R2 = -16384 (X);
    R6 = R0 >>> 31;
    R6 = R6 ^ R2;
    R6 = R6 + R7;
    R7 = R2;
    R6 = R6 ^ R7;
    R2 = [P2 + 28];
.Ls_out:
    R5 = R3 << 1;
.Ls_end:
    R3 = R6 ^ R7;
    W [P5 ++] = R5;
    [P2 + 20] = R0;
    [P2 + 24] = R1;
    R3 = R3 - R7;
    [P2 + 32] = R3;
    R4 = -R4;
    [P2 + 36] = R4;
    /* ---- crossfade ---- */
    P5 = [SP + 136];
    R0 = SP;
    R0 += 30;
    I0 = R0;
    R0 += 52;
    I1 = R0;
    P1 = M3;
    R4 = [P2 + 40];
    R2 = [P2 + 44];
    R1 = [P2 + 48];
    CC = R2 == 0;
    IF !CC JUMP .Lx_moving;
    /* fixed balance: R3 = [65535 - w : w] */
    R3 = R4 << 1;
    R6 = ~R3;
    R3 = PACK (R6.L, R3.L);
    R7 = 3;
    R0.L = W [I0 ++];
    R1.L = W [I1 ++];
    A1 = R0.L * R3.H (M);
    LSETUP (.Lx_top, .Lx_end) LC0 = P1;
.Lx_top:
    R5.H = (A1 += R1.L * R3.L) (M, T) || R0.L = W [I0 ++];
    R5 = R5 >>> 18 || R1.L = W [I1 ++];
    R5 = R5 * R7 (IS, NS);
.Lx_end:
    A1 = R0.L * R3.H (M) || W [P5 ++] = R5;
    JUMP .Ldone;
.Lx_moving:
    /* p = delta * crossfade, stepping by delta * step: R7 */
    R2 *= R1;
    R7 = R2;
    R0.L = W [I0 ++];
    LSETUP (.Ly_top, .Ly_end) LC0 = P1;
.Ly_top:
    R3 = R7 >>> 15 || R1.L = W [I1 ++];
    R3 = (R3 + R4) << 1;
    R6 = ~R3;
    A1 = R0.L * R6.L (M);
    R5.H = (A1 += R1.L * R3.L) (M, T);
    R5 = R5 >>> 18;
    R6 = R5 + R5 (NS);
    R5 = R5 + R6 (NS);
.Ly_end:
    R7 = R7 + R2 (NS) || W [P5 ++] = R5 || R0.L = W [I0 ++];
.Ldone:
    SP += 60;
    SP += 60;
    SP += 20;
    (R7:4, P5:3) = [SP++];
    RTS;
    .size _opxy_square_sync, .-_opxy_square_sync
