/* opxy::kernels::blep_square on the device: sdk/include/opxy/kernels/blep_square.hpp
 * holds the definition and the portable reference this matches sample for sample.
 *
 * void opxy_blep_square(const BlepSquareCall* calls, i16* out, unsigned size, unsigned count)
 * renders `count` oscillators in turn, each written or added (its mix flag).
 * BlepSquareCall (28 bytes): 0 state, 4 K = gain * 512, 8 mix, 12 reciprocal,
 * 16 divisor, 20 increment step, 24 gain (0..63, the same for every call).
 * BlepSquare: 0 phase, 4 increment, 8 increment step, 12 next sample, 16 high (byte).
 * The header calls only in range: the square matches its phase's half and every
 * increment is below 2^31, so a sample crosses at most one edge: a rise when
 * the phase enters the upper half, a fall when it wraps.
 *
 * The phase is kept biased by the square's state (pb = phase + high * 2^31), so
 * a sample has an edge exactly when pb is negative. The pending value of the
 * next sample is kept as [N : OUT] (N = next - 16384, 16 bits, and OUT its
 * scaled output); a sample without an edge writes OUT and takes the steady
 * pending value of its state from the state record. Edge samples branch out:
 *   n  = the phase past the edge (pb + 2^31 for either edge);
 *   t  = min(n / (increment >> 16), 65535), by divide steps, or with a fixed
 *        divisor d in 16385..32767 by the reciprocal R = 2^31 / d - 65536:
 *        t0 = (n + nh * R + (nl * R >> 16)) >> 15 is t or t - 1 (n < 2^31),
 *        corrected once against the remainder, then clamped;
 *   [Nx : This] = [(65535 - t)^2 : t^2] >> 18 by unsigned truncating multiplies;
 *   [N' : this] = [B : N] + [-s Nx : s This], with s = +1, B = 16383 rising and
 *        s = -1, B = -16384 falling, from the state record;
 *   outputs: (int16)(v << 1) * (gain * 512) with the truncating multiply (T),
 *   added to the buffer's sample (mix) or to zero.
 * Loads in the loops are relative to the current state record (below) or,
 * for this call's values, to the call (P1 once the loop is set up).
 * The loop's last instruction stores the previous [N : OUT]'s OUT while it
 * replaces it (a parallel store reads its register before the write).
 * Clobbers R0..R3, P0..P2, I0, I1, I3, M1, B2, B3, A0, A1, LC0/LT0/LB0,
 * LC1/LT1/LB1, ASTAT.
 */
.section .text
.align 4
.global _opxy_blep_square
.type _opxy_blep_square, STT_FUNC
_opxy_blep_square:
    R3 = [SP + 12];                   /* count */
    [--SP] = (R7:4);
    I1 = R0;                          /* this oscillator's call, P1 in the loops */
    B2 = R1;                          /* out */
    B3 = R2;                          /* samples */
    P0 = R0;
    P1 = R3;
    R6 = 28 (X);
    M1 = R6;                          /* one call */
    P2.L = _opxy_blep_square_records;
    P2.H = _opxy_blep_square_records;
    R7 = [P0 + 24];
    R6 = [P2 + 96];
    LSETUP(.Loscillator, .Losc_end) LC1 = P1;
    CC = R6 == R7;
    IF !CC JUMP .Lgain;
.Loscillator:
    P0 = I1;
    I3 = B2;                          /* out, written */
    I0 = B2;                          /* out, read when mixing */
    P1 = B3;                          /* samples */
    P2.L = _opxy_blep_square_records;
    P2.H = _opxy_blep_square_records;
    R4 = [P0 + 8];                    /* mix */
    R2 = [P0 + 20];                   /* increment step */
    R5 = [P0 + 12];                   /* reciprocal */
    R7 = [P0 + 4];                    /* K */
    P0 = [P0];                        /* state */
    R0 = [P0];
    R1 = [P0 + 4];
    R6 = B[P0 + 16] (Z);
    CC = R6 == 0;
    IF CC JUMP .Lstate;
    P2 += 48;
.Lstate:
    R6 <<= 31;
    R0 = R0 + R6 (NS) || R3 = [P0 + 12] || NOP;   /* biased phase */
    R6 = 16384 (X);
    R3 = R3 - R6 (NS);                /* N = next - 16384 */
    R6 = R3 << 1;
    R6.L = R6.L * R7.L (T);
    R3 = PACK(R3.L, R6.L);            /* [N : f(N)] */
    CC = R4 == 0;                     /* first: add to zero */
    IF CC JUMP .Lfirst;
    CC = R5 == 0;
    IF CC JUMP .Lmix_divide;
    P0.L = .Lmr_end;
    P0.H = .Lmr_end;
    LSETUP(.Lmr_start, .Lmr_end) LC0 = P1;
    P1 = I1;
.Lmr_start:
    R1 = R1 + R2 (NS) || R4 = [P2] || R6.L = W[I0++];
    R0 = R0 + R1 (NS) || R7 = [P2 + 36] || NOP;
    CC = R0 < 0;
    IF CC JUMP .Lr_edge;
    R3.L = R6.L + R3.L (NS);
.Lmr_end:
    R3 = PACK(R4.H, R4.L) || W[I3++] = R3.L || R2 = [P1 + 20];
    JUMP .Ldone;
.Lmix_divide:
    P0.L = .Lmd_end;
    P0.H = .Lmd_end;
    LSETUP(.Lmd_start, .Lmd_end) LC0 = P1;
    P1 = I1;
.Lmd_start:
    R1 = R1 + R2 (NS) || R4 = [P2] || R6.L = W[I0++];
    R0 = R0 + R1 (NS) || R7 = [P2 + 36] || NOP;
    CC = R0 < 0;
    IF CC JUMP .Ld_edge;
    R3.L = R6.L + R3.L (NS);
.Lmd_end:
    R3 = PACK(R4.H, R4.L) || W[I3++] = R3.L || R2 = [P1 + 20];
    JUMP .Ldone;
.Lfirst:
    R6 = 0;
    CC = R5 == 0;
    IF CC JUMP .Lfirst_divide;
    P0.L = .Lfr_end;
    P0.H = .Lfr_end;
    LSETUP(.Lfr_start, .Lfr_end) LC0 = P1;
    P1 = I1;
.Lfr_start:
    R1 = R1 + R2 (NS) || R4 = [P2] || NOP;
    R0 = R0 + R1 (NS) || R7 = [P2 + 36] || NOP;
    CC = R0 < 0;
    IF CC JUMP .Lr_edge;
.Lfr_end:
    R3 = PACK(R4.H, R4.L) || W[I3++] = R3.L || R2 = [P1 + 20];
    JUMP .Ldone;
.Lfirst_divide:
    P0.L = .Lfd_end;
    P0.H = .Lfd_end;
    LSETUP(.Lfd_start, .Lfd_end) LC0 = P1;
    P1 = I1;
.Lfd_start:
    R1 = R1 + R2 (NS) || R4 = [P2] || NOP;
    R0 = R0 + R1 (NS) || R7 = [P2 + 36] || NOP;
    CC = R0 < 0;
    IF CC JUMP .Ld_edge;
.Lfd_end:
    R3 = PACK(R4.H, R4.L) || W[I3++] = R3.L || R2 = [P1 + 20];

.Ldone:
    /* Write back: phase (unbiased), increment, next sample, high. */
    P0 = [P1];
    R6 = [P2 + 40];                   /* the bias */
    R7 = [P2 + 44];
    R0 = R0 + R6 (NS) || R4 = [P2 + 32] || NOP;   /* 16384 */
    R3 = R3 >>> 16 || [P0] = R0 || NOP;
    R3 = R3 + R4 (NS) || [P0 + 4] = R1 || NOP;
    B[P0 + 16] = R7;
    I1 += M1;                         /* the next call */
.Losc_end:
    [P0 + 12] = R3;
    (R7:4) = [SP++];
    RTS;

.Lgain:
    /* A new gain: both records' steady [N : f(N)]. */
    [P2 + 96] = R7;
    R7 = [P0 + 4];
    R5.L = 16383;
    R5.H = -16384;
    R4 = R5 << 1 (V);
    R4.H = R4.H * R7.L, R4.L = R4.L * R7.L (T);
    R6 = PACK(R5.H, R4.H);
    [P2] = R6;
    R6 = PACK(R5.L, R4.L);
    [P2 + 48] = R6;
    JUMP .Loscillator;

.Lr_edge:
    R7 = R0 + R7 (NS) || R2 = [P1 + 12] || NOP;           /* n; R */
    R5 = R7.H * R2.L, R4 = R7.L * R2.L (FU);
    R4 = R4 >> 16 || R2 = [P1 + 16] || NOP;               /* d */
    R5 = R5 + R7 (NS);
    R5 = R5 + R4 (NS);
    R5 = R5 >> 15;                                        /* t0 */
    R4 = R5.L * R2.L (FU);
    R7 = R7 - R4 (NS);                                    /* n - t0 d */
    CC = R2 <= R7 (IU);
    R4 = CC;
    R5 = R5 + R4 (NS) || R2 = [P2 + 24] || NOP;           /* 65535 */
    R5 = MIN(R5, R2) || R7 = [P2 + 24] || NOP;            /* 0x0000ffff */
    JUMP .Lblep;

.Ld_edge:
    R5 = R0 + R7 (NS);                                    /* n */
    R2 = R1 >> 16 || R7 = [P2 + 24] || NOP;               /* d; 0x0000ffff */
    R4 = R5 >> 16;
    CC = R4 < R2 (IU);
    DIVS (R5, R2);
    DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2);
    DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2);
    DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2);
    DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2); DIVQ (R5, R2);
    IF !CC R5 = R7;                                       /* clamp: 65535 */

.Lblep:
    /* R5.L = t, R7 = 0x0000ffff. */
    R5 = PACK(R5.L, R5.L);
    R5 = R5 ^ R7;                                         /* [t : 65535 - t] */
    R5.H = R5.H * R5.H, R5.L = R5.L * R5.L (TFU) || R2 = [P2 + 4] || NOP;  /* B */
    R5 = R5 >> 2 (V) || R7 = [P2 + 8] || NOP;             /* [This : Nx]; [s : -s] */
    R4 = PACK(R3.H, R2.L);                                /* [N : B] */
    R5.H = R5.H * R7.H, R5.L = R5.L * R7.L (IS) || R2 = [P1 + 4] || NOP;  /* K */
    R4 = R4 +|+ R5;                                       /* [this : N'] */
    R5 = R4 << 1 (V);
    R5.H = R5.H * R2.L, R5.L = R5.L * R2.L (T) || R7 = [P2 + 36] || NOP;
    R4 = PACK(R4.L, R5.L);                                /* pending [N' : f(N')] */
    R3.L = R6.L + R5.H (NS);                              /* this sample, stored by the loop */
    R0 = R0 + R7 (NS) || P2 = [P2 + 12] || NOP;           /* flip the bias and the record */
    JUMP (P0);                                            /* the loop's last instruction */
.size _opxy_blep_square, .-_opxy_blep_square

/* State records, 48 bytes each: 0 steady [N : OUT], 4 B, 8 [s : -s], 12 the
 * other record, 24 65535, 32 16384, 36 2^31, 40 the phase bias (high * 2^31),
 * 44 high (16, 20, 28 unused). Then the gain the steady values are for. */
.section .data
.align 4
.type _opxy_blep_square_records, STT_OBJECT
_opxy_blep_square_records:
    .long 0, 16383, 0x0001ffff, _opxy_blep_square_records + 48
    .long 0, 0, 0xffff, 0, 16384, 0x80000000, 0, 0
    .long 0, 0xc000, 0xffff0001, _opxy_blep_square_records
    .long 0, 0, 0xffff, 0, 16384, 0x80000000, 0x80000000, 1
    .long 0xffffffff
.size _opxy_blep_square_records, .-_opxy_blep_square_records
