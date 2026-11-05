/* opxy::kernels::additive on the device: sdk/include/opxy/kernels/additive.hpp
 * holds the definition and the portable reference this matches sample for sample.
 *
 * void opxy_additive(Additive* s, i16* out, unsigned pairs): pairs output pairs
 * (2 * pairs samples), amplitudes and targets in 0..32767 with each set summing
 * to at most 32767 (the header checks the targets; the amplitudes then stay so).
 *
 * One zero-overhead hardware loop per output pair, 116 cycles: per harmonic,
 * its phase, table address and interpolated sine (7); per two harmonics, one
 * dual fractional multiply by both amplitudes, a two-lane sum and the two-lane
 * one-pole step (5). The loop is rotated by two instructions so that the next
 * pair's phase step and first shift carry this pair's two stores.
 * Exactness:
 *   - The sine read is value + (slope * fraction >> 16), fraction the signed
 *     16 bits phase >> 8: an (IS) 16 x 16 product whose high half is the shift,
 *     added to the value in 16 bits as the C int16 result keeps.
 *   - q31 multiply(sine << 16, a) = floor(sine * a / 2^15): the (T) fractional
 *     product's truncated high half. a >= 0, so -32768 * -32768 cannot occur.
 *   - Each term lies in [-a, a), so with the amplitudes summing to at most 32767
 *     the lane sums and their total fit 16 bits: the 16-bit adds are the C sum.
 *   - (target - a) >> 8 fits a 16-bit lane before and after the shift.
 *   - The first sample of a pair is (out + previous) >> 1: a 16-bit add and
 *     subtract (ASR), whose 17-bit sum is halved before it is kept.
 * Clobbers R0..R3, P0..P2, I0, I1, B0, B1 (L0 and L1 are left 0), A0, A1,
 * LC0/LT0/LB0, ASTAT.
 */
/* Struct offsets (Additive): 0 phase, 4 increment, 8 previous (16 bits),
 * 12 amplitude pairs, 16 target pairs, 20 sine table. Registers in the loop:
 * R7 phase, R6 harmonic phase, R5 lane sums, R4 the two sines, P4 sine table,
 * I1 amplitude pairs and I0 target pairs (both circular over 6 words), P5 out. */

.section .text
.align 4
.global _opxy_additive
.type _opxy_additive, STT_FUNC
_opxy_additive:
    CC = R2 == 0;
    IF CC JUMP 9f;
    [--SP] = (R7:4, P5:4);
    P2 = R0;
    P5 = R1;
    P1 = R2;
    R0 = [P2 + 12];
    B1 = R0; I1 = R0; L1 = 24;
    R0 = [P2 + 16];
    B0 = R0; I0 = R0; L0 = 24;
    P4 = [P2 + 20];
    R7 = [P2];
    R4 = [P2 + 4];
    R7 = R7 + R4;
    R0 = R7 >> 23;
    LSETUP (1f, 2f) LC0 = P1;
1:  P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R7 >> 8 || R2 = [P0];
    R3 = R2.H * R1.L (IS);
    R4.L = R2.L + R3.H (NS);
    R6 = R7 + R7 (NS);
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0] || R0 = [I1];
    R3 = R2.H * R1.L (IS);
    R4.H = R2.L + R3.H (NS) || R1 = [I0++];
    R3.H = (A1 = R4.H * R0.H), R3.L = (A0 = R4.L * R0.L) (T);
    R5 = R3;
    R1 = R1 -|- R0;
    R1 = R1 >>> 8 (V);
    R0 = R0 +|+ R1;
    R6 = R6 + R7 (NS) || [I1++] = R0;
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0];
    R3 = R2.H * R1.L (IS);
    R4.L = R2.L + R3.H (NS);
    R6 = R6 + R7 (NS);
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0] || R0 = [I1];
    R3 = R2.H * R1.L (IS);
    R4.H = R2.L + R3.H (NS) || R1 = [I0++];
    R3.H = (A1 = R4.H * R0.H), R3.L = (A0 = R4.L * R0.L) (T);
    R5 = R3 +|+ R5;
    R1 = R1 -|- R0;
    R1 = R1 >>> 8 (V);
    R0 = R0 +|+ R1;
    R6 = R6 + R7 (NS) || [I1++] = R0;
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0];
    R3 = R2.H * R1.L (IS);
    R4.L = R2.L + R3.H (NS);
    R6 = R6 + R7 (NS);
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0] || R0 = [I1];
    R3 = R2.H * R1.L (IS);
    R4.H = R2.L + R3.H (NS) || R1 = [I0++];
    R3.H = (A1 = R4.H * R0.H), R3.L = (A0 = R4.L * R0.L) (T);
    R5 = R3 +|+ R5;
    R1 = R1 -|- R0;
    R1 = R1 >>> 8 (V);
    R0 = R0 +|+ R1;
    R6 = R6 + R7 (NS) || [I1++] = R0;
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0];
    R3 = R2.H * R1.L (IS);
    R4.L = R2.L + R3.H (NS);
    R6 = R6 + R7 (NS);
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0] || R0 = [I1];
    R3 = R2.H * R1.L (IS);
    R4.H = R2.L + R3.H (NS) || R1 = [I0++];
    R3.H = (A1 = R4.H * R0.H), R3.L = (A0 = R4.L * R0.L) (T);
    R5 = R3 +|+ R5;
    R1 = R1 -|- R0;
    R1 = R1 >>> 8 (V);
    R0 = R0 +|+ R1;
    R6 = R6 + R7 (NS) || [I1++] = R0;
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0];
    R3 = R2.H * R1.L (IS);
    R4.L = R2.L + R3.H (NS);
    R6 = R6 + R7 (NS);
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0] || R0 = [I1];
    R3 = R2.H * R1.L (IS);
    R4.H = R2.L + R3.H (NS) || R1 = [I0++];
    R3.H = (A1 = R4.H * R0.H), R3.L = (A0 = R4.L * R0.L) (T);
    R5 = R3 +|+ R5;
    R1 = R1 -|- R0;
    R1 = R1 >>> 8 (V);
    R0 = R0 +|+ R1;
    R6 = R6 + R7 (NS) || [I1++] = R0;
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0];
    R3 = R2.H * R1.L (IS);
    R4.L = R2.L + R3.H (NS);
    R6 = R6 + R7 (NS);
    R0 = R6 >> 23;
    P1 = R0;
    P0 = P4 + (P1 << 2);
    R1 = R6 >> 8 || R2 = [P0] || R0 = [I1];
    R3 = R2.H * R1.L (IS);
    R4.H = R2.L + R3.H (NS) || R1 = [I0++];
    R3.H = (A1 = R4.H * R0.H), R3.L = (A0 = R4.L * R0.L) (T) || [P2] = R7;
    R5 = R3 +|+ R5;
    R1 = R1 -|- R0 || R2 = W [P2 + 8] (X);
    R1 = R1 >>> 8 (V) || R4 = [P2 + 4];
    R0 = R0 +|+ R1;
    R1.L = R5.L + R5.H (NS) || [I1++] = R0;
    R2 = R1 +|+ R2, R3 = R1 -|- R2 (ASR) || W [P2 + 8] = R1;
    R7 = R7 + R4 (NS) || W [P5 ++] = R2;
2:  R0 = R7 >> 23 || W [P5 ++] = R1;
    L0 = 0;
    L1 = 0;
    (R7:4, P5:4) = [SP++];
9:  RTS;
.size _opxy_additive, .-_opxy_additive
