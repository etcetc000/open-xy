/* Platform-owned BF707 sample kernel: the second pass of a sample voice
 * (opxy/sample_kernel.hpp). Core machine sources contain no assembly; the C
 * reference (opxy::sample_interpolate_reference) defines every output word.
 *
 * void opxy_sample_interpolate(i32* output, const u32* positions, u32 count,
 *                              const opxy::SampleTaps* taps);
 * ABI (bfin-elf GCC): R0 = output, R1 = positions, R2 = count, [SP + 12] =
 * taps. Saves and restores R4-R7 and P3-P5. Clobbers only call-clobbered
 * registers: R0-R3, P0-P2, I0, B0, M0, M1, A0, A1, ASTAT (CC and the
 * arithmetic flags; no mode bit), LC0/LT0/LB0. It writes no L register (L0
 * stays zero, as the C ABI guarantees), no mode bit, nothing in memory beyond
 * output[0 .. 2 * count - 1], and calls nothing; an interrupt may preempt it
 * anywhere.
 *
 * Alignment: positions and output are 4-byte aligned (their C types); origin
 * needs 2. A stereo origin that is not 4-byte aligned takes the split loops
 * (two 16-bit loads a frame), chosen once a call; an aligned call pays three
 * instructions for the test.
 *
 * taps: +0 origin (i16*), +4 shift, +8 last, +12 reverse (0/1), +16 channels
 * (1/2), +20 level (Q27; 1 << 27 is unity).
 *
 * Each frame: offset = position >> shift (negated when reverse), the next
 * frame's address one frame further in playback order, or the region's first
 * frame at or past last; fraction = (position << (32 - shift)) >> 18; each
 * channel (a * (16384 - f) + b * f) >> 14 from two 16 x 16 products; then
 * the level: unity  v << 15,
 *            below 2^28  v * high15 * 8 + ((v * low15 + bias) >> 12)
 *            otherwise   a second pass over the words (gain of v << 15).
 */
.text
.align 2

/* The frame loops: stereo or mono source, forward or reverse, unity or a
 * level below 2^28. P1 holds the count (at least 1) when one starts.
 * Software-pipelined by one frame: a frame's reads and products (FRONT)
 * overlap the previous frame's words and stores (BACK), so the stores and the
 * next position's load share cycles with arithmetic. The products are
 * fractional (2 * a * w), so the accumulators hold 2 * sum: unity words are
 * (2 * sum) & ~0x7fff = (sum >> 14) << 15, and v = (2 * sum) >> 15. */
.macro FRONT stereo, reverse, store, split=0
.if \store
    R1 = LSHIFT R0 BY R7.L || [P2++] = R2;
    R2 = LSHIFT R0 BY R6.L || [P2++] = R3;
.else
    R1 = LSHIFT R0 BY R7.L;
    R2 = LSHIFT R0 BY R6.L;
.endif
.if \reverse
    R1 = -R1;
.endif
    P0 = R1;
    R2 = R2 >> 18;
    CC = R4 <= R0 (IU);
.if \stereo
    P0 = P5 + (P0 << 2);
.else
    P0 = P5 + (P0 << 1);
.endif
    R2.H = R6.H - R2.L (NS);
.if \stereo
.if \split
    P3 = P0 + P4;
    IF CC P3 = P5;
    R1 = W[P0] (X);
    R0 = W[P0 + 2] (X);
    R1 = PACK (R0.L, R1.L);
    R3 = W[P3] (X);
    R0 = W[P3 + 2] (X);
    R3 = PACK (R0.L, R3.L);
    A1 = R1.H * R2.H, A0 = R1.L * R2.H;
.else
    R1 = [P0 ++ P4];
    IF CC P0 = P5;
    A1 = R1.H * R2.H, A0 = R1.L * R2.H || R3 = [P0];
.endif
.else
    R1 = W[P0 ++ P4] (X);
    IF CC P0 = P5;
    A0 = R1.L * R2.H || R3 = W[P0] (X);
.endif
.endm

/* The second products: the frame's last instruction. */
.macro LASTMAC stereo
.if \stereo
    A1 += R3.H * R2.L, A0 += R3.L * R2.L;
.else
    A0 += R3.L * R2.L;
.endif
.endm

/* The previous frame's words into R2 (left) and R3 (right); with next, the
 * next position into R0. */
.macro BACK stereo, small, next
.if \small
.if \stereo
    R3 = A1, R2 = A0;
.else
    R2 = A0;
.endif
    R2 >>>= 15;
    R0 = R2.L * R5.H (IS);
    R1 = R2.L * R5.L (IS);
    R0 <<= 3;
    R2 >>>= 31;
    R2 >>= 20;
    R1 = R1 + R2;
    R1 >>>= 12;
    R2 = R0 + R1;
.if \stereo
    R3 >>>= 15;
    R0 = R3.L * R5.H (IS);
    R1 = R3.L * R5.L (IS);
    R0 <<= 3;
    R3 >>>= 31;
    R3 >>= 20;
    R1 = R1 + R3;
    R1 >>>= 12;
    R3 = R0 + R1;
.else
    R3 = R2;
.endif
.if \next
    R0 = [I0++];
.endif
.else
.if \stereo
.if \next
    R3 = A1, R2 = A0 || R0 = [I0++];
.else
    R3 = A1, R2 = A0;
.endif
    R2 = R2 & R5;
    R3 = R3 & R5;
.else
.if \next
    R2 = A0 || R0 = [I0++];
.else
    R2 = A0;
.endif
    R2 = R2 & R5;
    R3 = R2;
.endif
.endif
.endm

.macro FRAMES stereo, reverse, small, split=0
    R0 = [I0++];
    FRONT \stereo, \reverse, 0, \split
    LASTMAC \stereo
    P1 += -1;
    CC = P1 == 0;
    IF CC JUMP 3f;
    LSETUP (1f, 2f) LC0 = P1;
1:  BACK \stereo, \small, 1
    FRONT \stereo, \reverse, 1, \split
2:  LASTMAC \stereo
3:  BACK \stereo, \small, 0
    [P2++] = R2;
    [P2++] = R3;
.if \split
    JUMP .Lsplit_done;
.else
    JUMP .Ldone;
.endif
.endm

.global _opxy_sample_interpolate
.type _opxy_sample_interpolate, STT_FUNC
_opxy_sample_interpolate:
    CC = R2 == 0;
    IF !CC JUMP 9f;
    JUMP.L .Lnone;
9:
    [--SP] = (R7:4, P5:3);
    P3 = [SP + 40];
    I0 = R1;
    P2 = R0;
    P1 = R2;
    B0 = R0;                    /* the output, for the level pass */
    M0 = R2;                    /* the count, for the level pass */
    P5 = [P3];                  /* origin */
    R0 = [P3 + 4];              /* shift */
    R4 = [P3 + 8];              /* last */
    R1 = [P3 + 12];             /* reverse */
    R2 = [P3 + 16];             /* channels */
    R3 = [P3 + 20];             /* level */
    M1 = R3;                    /* kept for the level pass */
    R7 = -R0;                   /* .L: right shift by `shift` */
    R6 = 32;
    R6 = R6 - R0;               /* .L: left shift by 32 - shift */
    R6.H = 0x4000;              /* .H: 16384 */
    /* P4: bytes from a frame to the next in playback order. */
    R5 = R2 << 1;
    CC = R1 == 0;
    R0 = -R5;
    IF !CC R5 = R0;
    P4 = R5;
    /* A stereo origin that is 2 but not 4-byte aligned takes the split loops:
     * two 16-bit loads a frame instead of one 32-bit load, which would fault. */
    R0 = P5;
    CC = BITTST (R0, 1);
    IF !CC JUMP 9f;
    JUMP.L .Lsplit_check;
9:
.Laligned:
    /* Level: unity, below 2^28 (15-bit halves in R5), or any (16-bit halves). */
    R0.H = 0x0800;
    R0.L = 0;
    CC = R3 == R0;
    IF !CC JUMP 9f;
    JUMP.L .Lunity;
9:
    R0.H = 0x1000;
    CC = R3 < R0 (IU);
    IF CC JUMP 9f;
    JUMP.L .Lany;
9:
    R0 = R3 >> 15;
    R5 = R3 << 17;
    R5 >>= 17;
    R5 = PACK(R0.L, R5.L);
    CC = R1 == 0;
    IF CC JUMP 9f;
    JUMP.L .Lsmall_reverse;
9:
    CC = R2 == 2;
    IF CC JUMP 9f;
    JUMP.L .Lsmall_mono_forward;
9:
    FRAMES 1, 0, 1
.Lsmall_mono_forward:
    FRAMES 0, 0, 1
.Lsmall_reverse:
    CC = R2 == 2;
    IF CC JUMP 9f;
    JUMP.L .Lsmall_mono_reverse;
9:
    FRAMES 1, 1, 1
.Lsmall_mono_reverse:
    FRAMES 0, 1, 1
.Lany:
.Lunity:
    R5.H = 0xffff;
    R5.L = 0x8000;              /* the unity words' mask */
    CC = R1 == 0;
    IF CC JUMP 9f;
    JUMP.L .Lunity_reverse;
9:
    CC = R2 == 2;
    IF CC JUMP 9f;
    JUMP.L .Lunity_mono_forward;
9:
    FRAMES 1, 0, 0
.Lunity_mono_forward:
    FRAMES 0, 0, 0
.Lunity_reverse:
    CC = R2 == 2;
    IF CC JUMP 9f;
    JUMP.L .Lunity_mono_reverse;
9:
    FRAMES 1, 1, 0
.Lunity_mono_reverse:
    FRAMES 0, 1, 0
.Lsplit_check:
    CC = R2 == 2;
    IF CC JUMP 9f;
    JUMP.L .Laligned;           /* mono needs 2-byte alignment only */
9:
    R5.H = 0xffff;
    R5.L = 0x8000;
    CC = R1 == 0;
    IF CC JUMP 9f;
    JUMP.L .Lsplit_reverse;
9:
    FRAMES 1, 0, 0, 1
.Lsplit_reverse:
    FRAMES 1, 1, 0, 1
.Lsplit_done:
    /* The split loops write unity words; any other level takes the level pass. */
    R3 = M1;
    R0.H = 0x0800;
    R0.L = 0;
    CC = R3 == R0;
    IF !CC JUMP 9f;
    JUMP.L .Lout;
9:
    JUMP.L .Llevel_any;
.Ldone:
    R3 = M1;
    /* A level of 2^28 or more: each word w = v << 15 becomes
     * sign(v) * min((|v| * level) >> 12, 2^31 - 1 (+1 when negative)). */
    R0.H = 0x0800;
    R0.L = 0;
    CC = R3 == R0;
    IF !CC JUMP 9f;
    JUMP.L .Lout;
9:
    R0.H = 0x1000;
    CC = R3 < R0 (IU);
    IF !CC JUMP 9f;
    JUMP.L .Lout;
9:
.Llevel_any:
    R5 = R3;                    /* .L low 16 bits, .H high 16 bits */
    P2 = B0;
    P0 = M0;
    P0 = P0 + P0;               /* two words a frame */
    R4.H = 0x0800;
    R4.L = 0;                   /* 2^27: saturation bound of |v| * high16 */
    R7.H = 0x7fff;
    R7.L = 0xffff;
    LSETUP (.Llevel_begin, .Llevel_end) LC0 = P0;
.Llevel_begin:
    R0 = [P2];
    R0 >>>= 15;                 /* v */
    R6 = R0 >>> 31;             /* -1 when negative */
    R1 = ABS R0;                /* |v| <= 32768 */
    R2 = R1.L * R5.H (FU);      /* upper = |v| * high16 */
    R3 = R1.L * R5.L (FU);      /* |v| * low16 */
    R3 >>= 12;
    R1 = R2 << 4;
    R1 = R1 + R3;               /* r */
    R0 = R0 >> 31;              /* 1 when negative */
    R0 = R0 + R7;               /* the saturated magnitude */
    CC = R4 <= R2 (IU);
    IF CC R1 = R0;
    CC = R1 < 0;
    IF CC R1 = R0;
    R1 = R1 ^ R6;
    R1 = R1 - R6;
.Llevel_end:
    [P2++] = R1;
.Lout:
    (R7:4, P5:3) = [SP++];
.Lnone:
    RTS;
.size _opxy_sample_interpolate, .-_opxy_sample_interpolate

/* void opxy_sample_crossfade(i32* output, const opxy::SampleFade* fade, u32 count,
 *                            const opxy::SampleTaps* taps);
 * Two playheads mixed (opxy::sample_crossfade_reference defines it). ABI as
 * above: R0 = output, R1 = fade, R2 = count, [SP + 12] = taps. Saves and
 * restores R4-R7 and P3-P5; clobbers R0-R3, P0-P2, I0-I3, B0, M0, M1, A0,
 * A1, ASTAT (CC and the arithmetic flags; no mode bit), LC0/LT0/LB0. It
 * writes no L register (L0-L3 stay zero, as the C ABI guarantees, so I0-I3
 * address linearly), no mode bit, nothing beyond output[0 .. 2 * count - 1];
 * calls nothing; an interrupt may preempt it anywhere.
 *
 * fade: +0 from (u32*), +4 to (u32*), +8 weights (u16*, 0 to 16384).
 * Each frame: both playheads' frames as in the interpolation loops (fractional
 * products, 2 * sum, v = (2 * sum) >> 15), the two values packed as halves;
 * then (a * (16384 - w) + b * w) as one more pair of fractional products, so
 * the unity word is (2 * mix) & ~0x7fff. A level other than unity runs the
 * level pass of opxy_sample_interpolate over the words afterwards (its
 * arithmetic is sample_level_word for every level). */
.macro FADE_STAGE stereo, reverse, from, split=0
.if \from
    R0 = [I0++];
.else
    R0 = [I1++];
.endif
    R1 = LSHIFT R0 BY R7.L;
    R2 = LSHIFT R0 BY R6.L;
.if \reverse
    R1 = -R1;
.endif
    P0 = R1;
    R2 = R2 >> 18;
    CC = R4 <= R0 (IU);
.if \stereo
    P0 = P5 + (P0 << 2);
.else
    P0 = P5 + (P0 << 1);
.endif
    R2.H = R6.H - R2.L (NS);
.if \stereo
.if \split
    P3 = P0 + P4;
    IF CC P3 = P5;
    R1 = W[P0] (X);
    R0 = W[P0 + 2] (X);
    R1 = PACK (R0.L, R1.L);
    R3 = W[P3] (X);
    R0 = W[P3 + 2] (X);
    R3 = PACK (R0.L, R3.L);
    A1 = R1.H * R2.H, A0 = R1.L * R2.H;
.else
    R1 = [P0 ++ P4];
    IF CC P0 = P5;
    A1 = R1.H * R2.H, A0 = R1.L * R2.H || R3 = [P0];
.endif
    A1 += R3.H * R2.L, A0 += R3.L * R2.L;
    R3 = A1, R2 = A0;
    R2 >>>= 15;
    R3 >>>= 15;
    R2 = PACK (R3.L, R2.L);     /* .H right, .L left */
.else
    R1 = W[P0 ++ P4] (X);
    IF CC P0 = P5;
    A0 = R1.L * R2.H || R3 = W[P0] (X);
    A0 += R3.L * R2.L;
    R2 = A0;
    R2 >>>= 15;
.endif
.endm

.macro FADES stereo, reverse, split=0
    LSETUP (1f, 2f) LC0 = P1;
1:  FADE_STAGE \stereo, \reverse, 1, \split
    I3 = R2;
    FADE_STAGE \stereo, \reverse, 0, \split
    R1.L = W[I2++];             /* the weight */
    R1.H = R6.H - R1.L (NS);    /* .H 16384 - w, .L w */
    R3 = I3;
.if \stereo
    A1 = R3.H * R1.H, A0 = R3.L * R1.H;
    A1 += R2.H * R1.L, A0 += R2.L * R1.L;
    R3 = A1, R2 = A0;
    R2 = R2 & R5;
    R3 = R3 & R5;
.else
    A0 = R3.L * R1.H;
    A0 += R2.L * R1.L;
    R2 = A0;
    R2 = R2 & R5;
    R3 = R2;
.endif
    [P2++] = R2;
2:  [P2++] = R3;
    JUMP.L .Lf_done;
.endm

.global _opxy_sample_crossfade
.type _opxy_sample_crossfade, STT_FUNC
_opxy_sample_crossfade:
    CC = R2 == 0;
    IF !CC JUMP 9f;
    RTS;
9:
    [--SP] = (R7:4, P5:3);
    P3 = [SP + 40];
    P0 = R1;
    P2 = R0;
    P1 = R2;
    B0 = R0;                    /* the output, for the level pass */
    M0 = R2;                    /* the count, for the level pass */
    R1 = [P0];
    I0 = R1;                    /* from */
    R1 = [P0 + 4];
    I1 = R1;                    /* to */
    R1 = [P0 + 8];
    I2 = R1;                    /* weights */
    P5 = [P3];                  /* origin */
    R0 = [P3 + 4];              /* shift */
    R4 = [P3 + 8];              /* last */
    R1 = [P3 + 12];             /* reverse */
    R2 = [P3 + 16];             /* channels */
    R3 = [P3 + 20];             /* level */
    M1 = R3;
    R7 = -R0;
    R6 = 32;
    R6 = R6 - R0;
    R6.H = 0x4000;
    R5 = R2 << 1;
    CC = R1 == 0;
    R0 = -R5;
    IF !CC R5 = R0;
    P4 = R5;
    R5.H = 0xffff;
    R5.L = 0x8000;              /* the unity words' mask */
    R0 = P5;
    CC = BITTST (R0, 1);
    IF !CC JUMP 9f;
    CC = R2 == 2;
    IF !CC JUMP 9f;
    CC = R1 == 0;
    IF CC JUMP 8f;
    JUMP.L .Lf_split_reverse;
8:
    FADES 1, 0, 1
.Lf_split_reverse:
    FADES 1, 1, 1
9:
    CC = R1 == 0;
    IF CC JUMP 9f;
    JUMP.L .Lf_reverse;
9:
    CC = R2 == 2;
    IF CC JUMP 9f;
    JUMP.L .Lf_mono_forward;
9:
    FADES 1, 0
.Lf_mono_forward:
    FADES 0, 0
.Lf_reverse:
    CC = R2 == 2;
    IF CC JUMP 9f;
    JUMP.L .Lf_mono_reverse;
9:
    FADES 1, 1
.Lf_mono_reverse:
    FADES 0, 1
.Lf_done:
    R3 = M1;
    R0.H = 0x0800;
    R0.L = 0;
    CC = R3 == R0;
    IF !CC JUMP 9f;
    (R7:4, P5:3) = [SP++];
    RTS;
9:
    JUMP.L .Llevel_any;
.size _opxy_sample_crossfade, .-_opxy_sample_crossfade
