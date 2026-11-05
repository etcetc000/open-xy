/* opxy::kernels::fold_pair on the device: sdk/include/opxy/kernels/fold_pair.hpp
 * holds the definition and the portable reference this matches sample for sample.
 *
 * void opxy_fold_pair(FoldPair* s, i16* out, unsigned size), size 1..24, every
 * control value in 0..32767 (the header checks before calling).
 *
 * Four passes over the chunk, each a zero-overhead hardware loop, with the
 * intermediate values in a stack frame:
 *   gains    per-sample packed gains G = [triangle:sine] and balance B = [1:v<<1]
 *            (one sample only, and then reused, when no control moves);
 *   sine     both half steps: interpolated sine, times its gain -> XS = [x2:x1];
 *   triangle both half steps in 16-bit lanes, times its gain -> XT = [x2:x1];
 *   fold     the four fold lookups, halved sums and the exact crossfade.
 * Exactness, per lane:
 *   - (a * b) >> 15 is the truncating fractional multiply (T): floor(2ab / 2^16);
 *     no operand pair is -32768 * -32768 in range (gains are 2048..32767, the
 *     crossfade is 0..32767 and |delta| <= 32767).
 *   - The table slopes are signed and the fractions unsigned: (M, IS) multiplies
 *     give the exact 32-bit product, whose high half is the C >> 16. A fold
 *     fraction f is placed as f << 8 so that the same high half is (slope*f) >> 8.
 *   - A fold index is (x + 32768) >> 8 = (x >> 8) + 128: the fold table bases are
 *     advanced by 128 entries and indexed by the signed x >> 8.
 *   - (start + 15) >> 4 reaches 32782 for a 15-bit start: a logical lane shift.
 *   - The crossfade: b * v and a * v + a both fit 32 signed bits; their wrapped
 *     difference is the C residual, whose high half plus a is the sample.
 * Clobbers R0..R3, P0..P2, I0..I3, M0..M2, A1, LC0/LT0/LB0, ASTAT.
 */
/* Struct offsets (FoldPair): 0 sine phase, 4 triangle phase, 8/12 increments,
 * 16/20 steps, 24 crossfade step, 28/32 sine start/delta, 36/40 triangle,
 * 44/48 balance, 52 sine table, 56 sine fold, 60 triangle fold.
 * Frame: four arrays of 24 words, each after a spare word that absorbs the
 * pipelined loops' first store and the fold loop's read past the end. */

.section .text
.align 4
.global _opxy_fold_pair
.type _opxy_fold_pair, STT_FUNC
_opxy_fold_pair:
    [--SP] = (R7:4, P5:3);
    LINK 416;
    /* P5 = s, keep out and size in B0/B1 (L0/L1 stay zero). */
    P5 = R0;
    B0 = R1;
    B1 = R2;
    P4 = R2;                          /* sample count */

    /* ---- gains: G and B per sample, or once when nothing moves ---- */
    R0 = [P5 + 32];
    R1 = [P5 + 40];
    R2 = [P5 + 48];
    R3 = R0 | R1;
    R3 = R3 | R2;
    CC = R3 == 0;
    P4 = B1;
    R3 = 4;
    IF !CC JUMP .Lmoving;
    P4 = 1;
    R3 = 0;
.Lmoving:
    M1 = R3;                          /* G stride: 4, or 0 to reuse G[0] */
    M2 = R3;                          /* B stride */
    R2 = PACK(R1.L, R0.L);            /* D = [triangle delta : sine delta] */
    R0 = [P5 + 28];
    R1 = [P5 + 36];
    R3 = PACK(R1.L, R0.L);            /* [triangle start : sine start] */
    R4.L = 2048; R4.H = 2048;
    R4 = R4 +|+ R3;                   /* S2048 */
    R5.L = 15; R5.H = 15;
    R3 = R3 +|+ R5;                   /* S15 */
    R0 = [P5 + 48];
    R1 = [P5 + 44];
    R5 = PACK(R1.L, R0.L);            /* [balance start : balance delta] */
    R6 = 0;
    R6.H = 1;                         /* B = [1 : v << 1] */
    R0 = [P5 + 24];
    R0 <<= 16;                        /* [step : xfade = 0] */
    P1 = SP;
    P2 = SP;
    P0 = 304;
    P1 = P1 + P0;
    P0 = 200;
    P2 = P2 + P0;
    LSETUP(.Lg_start, .Lg_end) LC0 = P4;
.Lg_start:
    R0.L = R0.L + R0.H (NS) || [P2++] = R7 || NOP;
    R7.H = R2.H * R0.L, R7.L = R2.L * R0.L (T);
    R6.L = R5.L * R0.L (T);
    R1 = R7 +|+ R3;
    R6.L = R6.L + R5.H (NS);
    R1 = R1 >> 4 (V);
    R6.L = R6.L << 1;
    R7 = R7 +|+ R4 || [P1++] = R6 || NOP;
.Lg_end:
    R7 = R7 -|- R1;
    [P2] = R7;

    /* ---- sine: XS = [x2 : x1], x = (interpolated sine * gain) >> 15 ---- */
    P4 = B1;
    P3 = [P5 + 52];
    R0 = [P5 + 0];
    R1 = [P5 + 8];
    R2 = [P5 + 16];
    I1 = SP;
    P1 = SP;
    R3 = 204;
    M0 = R3;
    I1 += M0;
    P0 = 0;
    P1 = P1 + P0;
    LSETUP(.Ls_start, .Ls_end) LC0 = P4;
.Ls_start:
    R1 = R1 + R2 (NS) || [P1++] = R7 || NOP;
    R3 = R1 >> 1;
    R0 = R0 + R3 (NS);
    R4 = R0 >> 24;
    P0 = R4;
    P0 = P3 + (P0 << 2);
    R4 = R0 >> 8 || R6 = [P0] || NOP;
    R5 = R6.H * R4.L (M, IS);
    R7.L = R5.H + R6.L (NS);
    R0 = R0 + R3 (NS);
    R4 = R0 >> 24;
    P0 = R4;
    P0 = P3 + (P0 << 2);
    R4 = R0 >> 8 || R6 = [P0] || NOP;
    R5 = R6.H * R4.L (M, IS);
    R7.H = R5.H + R6.L (NS) || R4 = [I1 ++ M1] || NOP;
.Ls_end:
    R7.H = R7.H * R4.L, R7.L = R7.L * R4.L (T);
    [P1] = R7;
    [P5 + 0] = R0;
    [P5 + 8] = R1;

    /* ---- triangle: XT = [x2 : x1], both half steps in 16-bit lanes ---- */
    R0 = [P5 + 4];
    R1 = [P5 + 12];
    R2 = [P5 + 20];
    R7.L = 0x8000; R7.H = 0x8000;
    I1 = SP;
    P1 = SP;
    R3 = 204;
    M0 = R3;
    I1 += M0;
    P0 = 100;
    P1 = P1 + P0;
    LSETUP(.Lt_start, .Lt_end) LC0 = P4;
.Lt_start:
    R1 = R1 + R2 (NS) || [P1++] = R5 || NOP;
    R3 = R1 >> 1;
    R5 = R0 + R3 (NS);
    R0 = R5 + R3 (NS);
    R4 = PACK(R0.H, R5.H) || R3 = [I1 ++ M1] || NOP;
    R5 = R4 << 1 (V);
    R6 = R4 >>> 15 (V);
    R5 = R5 ^ R6;
    R5 = R5 +|+ R7;
.Lt_end:
    R5.H = R5.H * R3.H, R5.L = R5.L * R3.H (T);
    [P1] = R5;
    [P5 + 4] = R0;
    [P5 + 12] = R1;

    /* ---- fold and crossfade ---- */
    P0 = [P5 + 56];
    P1 = [P5 + 60];
    P4 = 512;
    P5 = P1 + P4;                     /* triangle fold entry 128 */
    P4 = P0 + P4;                     /* sine fold entry 128 */
    I0 = SP;
    I1 = SP;
    I2 = SP;
    R3 = 4;
    M0 = R3;
    I0 += M0;
    R3 = 104;
    M0 = R3;
    I1 += M0;
    R3 = 304;
    M0 = R3;
    I2 += M0;
    I3 = B0;
    P0 = B1;
    R0 = [I0++];
    R1 = [I1++];
    R2 = R0 >>> 24;
    LSETUP(.Lf_start, .Lf_end) LC0 = P0;
.Lf_start:
    R3 = R0 << 16;
    R3 = R3 >>> 24;
    P0 = R2;
    P0 = P4 + (P0 << 2);
    P1 = R3;
    P1 = P4 + (P1 << 2);
    R2 = R1 >>> 24;
    R3 = R1 << 16;
    R3 = R3 >>> 24;
    P2 = R2;
    P2 = P5 + (P2 << 2);
    P3 = R3;
    P3 = P5 + (P3 << 2);
    R0 = R0 << 8 (V) || R2 = [P1] || NOP;
    R1 = R1 << 8 (V) || R3 = [P3] || NOP;
    R5 = R2.H * R0.L (M, IS) || R4 = [P0] || NOP;
    R6.L = R5.H + R2.L (NS) || R7 = [P2] || NOP;
    R5 = R3.H * R1.L (M, IS);
    R6.H = R5.H + R3.L (NS);
    R5 = R4.H * R0.H (M, IS);
    R2.L = R5.H + R4.L (NS);
    R5 = R7.H * R1.H (M, IS);
    R2.H = R5.H + R7.L (NS);
    R6 = R6 >>> 1 (V);
    R2 = R2 >>> 1 (V) || R3 = [I2 ++ M2] || NOP;
    R6 = R6 +|+ R2;
    R5 = R6.H * R3.L (M, IS) || R0 = [I0++] || NOP;
    A1 = R6.L * R3.L (M, IS) || R1 = [I1++] || NOP;
    R7 = (A1 += R6.L * R3.H) (M, IS);
    R5 = R5 - R7 (NS);
    R6.L = R5.H + R6.L (NS);
.Lf_end:
    R2 = R0 >>> 24 || W[I3++] = R6.L || NOP;

    UNLINK;
    (R7:4, P5:3) = [SP++];
    RTS;
.size _opxy_fold_pair, .-_opxy_fold_pair
