/* opxy::kernels::wave_quad on the device: sdk/include/opxy/kernels/wave_quad.hpp
 * holds the definition and the portable reference this matches sample for sample.
 *
 * void opxy_wave_quad(WaveQuad* s, i16* out, unsigned size), balance 1..65535
 * (the header checks before calling), any size.
 *
 * One zero-overhead hardware loop per sample, 56 cycles: each stream takes 14,
 * with every load and store in a parallel slot. The phases stay in the struct;
 * the loop is rotated by one instruction so that the next sample's first phase
 * step carries this sample's store.
 * Exactness, per stream:
 *   - A read keeps 16 bits: value + floor(slope * f / 2^16) mod 2^16. The 32-bit
 *     product slope * f wraps, but its bits 16..31 are those of the true product,
 *     and the high-half add (value + product.H) keeps exactly those 16 bits.
 *   - The crossfade l + floor((r - l) * w / 2^16) equals
 *     floor((l * (65536 - w) + r * w) / 2^16): one mixed-mode (signed x
 *     unsigned, unshifted) MAC pair in A1, truncated to its high half. It stays
 *     between l and r, so it fits 16 bits; w = 0 would need 65536 and is excluded.
 *   - The four values accumulate in A0 as x * 8192 fractional (2 * 8192 * x);
 *     the truncated high half is floor(sum / 4), the C sum >> 2.
 * Clobbers R0..R3, P0..P2, A0, A1, LC0/LT0/LB0, ASTAT.
 */
/* Struct offsets (WaveQuad): 0 wave a, 4 wave b, 8..20 phases, 24..36 increments,
 * 40 balance. Registers in the loop: P4 wave a, P3 = b - a, P2 struct, P5 out,
 * R6 = [65536 - w : w], R7 = [8192 : extract pattern for the 24-bit fraction]. */

.section .text
.align 4
.global _opxy_wave_quad
.type _opxy_wave_quad, STT_FUNC
_opxy_wave_quad:
    CC = R2 == 0;
    IF CC JUMP 9f;
    [--SP] = (R7:4, P5:3);
    P2 = R0;
    P5 = R1;
    P1 = R2;
    P4 = [P2];
    R0 = [P2 + 4];
    R1 = P4;
    R0 = R0 - R1;
    P3 = R0;
    R0 = [P2 + 40];
    R1 = - R0;
    R6 = PACK(R1.L, R0.L);
    R7.L = 0x0118;
    R7.H = 8192;
    R0 = [P2 + 8];
    R1 = [P2 + 24];
    R0 = R0 + R1;
    LSETUP (1f, 2f) LC0 = P1;
    /* stream 0 */
1:  R1 = R0 >> 25 || [P2 + 8] = R0;
    P1 = R1;
    P0 = P4 + (P1 << 2);
    R1 = EXTRACT(R0, R7.L) (Z) || R2 = [P0 ++ P3];
    R4 = R2 >>> 16 || R3 = [P0];
    R5 = R3 >>> 16 || R0 = [P2 + 12];
    R4 = R4 * R1 (IS, NS);
    R5 = R5 * R1 (IS, NS);
    R4.H = R2.L + R4.H (NS) || R1 = [P2 + 28];
    R5.H = R3.L + R5.H (NS);
    A1 = R4.H * R6.H (M);
    R4.H = (A1 += R5.H * R6.L) (M, T);
    A0 = R4.H * R7.H;
    /* stream 1 */
    R0 = R0 + R1 (NS);
    R1 = R0 >> 25 || [P2 + 12] = R0;
    P1 = R1;
    P0 = P4 + (P1 << 2);
    R1 = EXTRACT(R0, R7.L) (Z) || R2 = [P0 ++ P3];
    R4 = R2 >>> 16 || R3 = [P0];
    R5 = R3 >>> 16 || R0 = [P2 + 16];
    R4 = R4 * R1 (IS, NS);
    R5 = R5 * R1 (IS, NS);
    R4.H = R2.L + R4.H (NS) || R1 = [P2 + 32];
    R5.H = R3.L + R5.H (NS);
    A1 = R4.H * R6.H (M);
    R4.H = (A1 += R5.H * R6.L) (M, T);
    A0 += R4.H * R7.H;
    /* stream 2 */
    R0 = R0 + R1 (NS);
    R1 = R0 >> 25 || [P2 + 16] = R0;
    P1 = R1;
    P0 = P4 + (P1 << 2);
    R1 = EXTRACT(R0, R7.L) (Z) || R2 = [P0 ++ P3];
    R4 = R2 >>> 16 || R3 = [P0];
    R5 = R3 >>> 16 || R0 = [P2 + 20];
    R4 = R4 * R1 (IS, NS);
    R5 = R5 * R1 (IS, NS);
    R4.H = R2.L + R4.H (NS) || R1 = [P2 + 36];
    R5.H = R3.L + R5.H (NS);
    A1 = R4.H * R6.H (M);
    R4.H = (A1 += R5.H * R6.L) (M, T);
    A0 += R4.H * R7.H;
    /* stream 3 */
    R0 = R0 + R1 (NS);
    R1 = R0 >> 25 || [P2 + 20] = R0;
    P1 = R1;
    P0 = P4 + (P1 << 2);
    R1 = EXTRACT(R0, R7.L) (Z) || R2 = [P0 ++ P3];
    R4 = R2 >>> 16 || R3 = [P0];
    R5 = R3 >>> 16 || R0 = [P2 + 8];
    R4 = R4 * R1 (IS, NS);
    R5 = R5 * R1 (IS, NS);
    R4.H = R2.L + R4.H (NS) || R1 = [P2 + 24];
    R5.H = R3.L + R5.H (NS);
    A1 = R4.H * R6.H (M);
    R4.H = (A1 += R5.H * R6.L) (M, T);
    R2.L = (A0 += R4.H * R7.H) (T);
2:  R0 = R0 + R1 (NS) || W [P5 ++] = R2;
    (R7:4, P5:3) = [SP++];
9:  RTS;
.size _opxy_wave_quad, .-_opxy_wave_quad
