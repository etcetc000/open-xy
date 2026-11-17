/* The audio loop's per-track mixing without stock's dead weight
 * (docs/abi-1.2.1.md "Track mixing"): sdk_track_mix, the five bus mixes that
 * skip silence.
 *
 * Track mixing.
 *
 * Stock OS 1.2.1, the audio loop 8d510760, per sounding track (its silence
 * counter below 2): after the track's gain ramp, meter and monitor tap, five
 * loops of 128 words mix the track buffer (11903600, 64 stereo frames) into
 * five buses, 8d511014..8d511180:
 *   aux    [FP-0x7c] += sat(R5 * x (T) << 11)
 *   tape   84caa740  += R6 * x (T)
 *   FX I   84caa140  += g * x (T), g = [FP-8] + [FP-0xc], stepping by [FP-0xc]
 *          per word (stock always stores 0 there: R4) and storing the last g
 *          back to [FP-8]
 *   FX II  84caa340  += R7 * x (T)
 *   master [SP+0x58] += g * x (T), g ramping per word from the track's ramp
 *          state ([FP-0x34]: +0 step, +4 gain, +8 halfword shift, +0xa snap
 *          byte) towards [FP-0x50] * [[FP+0xc]] (T), [FP+0xc] stepping by
 *          [SP+0x48] into P3, which stock reads after (8d5111b4).
 * Every add saturates (S). A product with a zero operand is exactly 0 ((T)
 * truncates the 64-bit product, so 0 * x is 0, with no rounding offset), and
 * sat(b + 0) is b: a loop whose gain is 0, or whose source is all zero, leaves
 * its bus bit for bit as it was. Nothing in a loop has state but the two gain
 * sequences, which this code still advances exactly.
 *
 * The hook replaces 8d511014 (R0 = [P2++]) and the aux loop's LSETUP with a
 * jump here, after stock's stage timing of the tap (R1, the first aux term, and
 * P3, are recomputed from the buffer). It runs the same five
 * mixes, skipping each loop whose gain is 0, and all of them when the buffer
 * is all zero (an early-exit scan: a few cycles while the track sounds), then
 * jumps to 8d511182 with what stock leaves live there: P3, memory, and R4 (0),
 * P4, P5, FP and SP untouched. R0-R3, R5-R7, P0-P2, I0, I1 and LC0 are dead
 * at 8d511182 (each is written before it is read again).
 *
 * Builds without profiling also jump here from 8d510f04, skipping stock's end
 * of the tap's stage timer (slot 17, 08035228: last, max, min, sum, count;
 * once a sounding track and block). Nothing here reads a register that code
 * writes (R0-R3, P0-P3, I0) before writing it. */
.section .text
.align 2

/* dst[k] = sat(dst[k] + g * buf[k] (T)), k = 0..127, two instruction groups a
 * word as stock's loops. Uses R0, R1, P0, P2, I1; P3 holds 127. */
.macro MIX g, dh, dl
    P0.L = 0x3600; P0.H = 0x1190;
    P2.L = \dl; P2.H = \dh;
    I1 = P2;
    R1 = [P0++];
    R1 = \g * R1 (T);
    R0 = [I1++];
    LSETUP (1f, 2f) LC0 = P3;
1:  R0 = R0 + R1 (S) || R1 = [P0++] || NOP;
2:  R1 = \g * R1 (T) || [P2++] = R0 || R0 = [I1++];
    R0 = R0 + R1 (S);
    [P2] = R0;
.endm

.global sdk_track_mix
.type sdk_track_mix, STT_FUNC
sdk_track_mix:
    /* Is any word of the buffer nonzero? Both halves at once, 8 words a pass,
     * leaving at the first pass that finds one. */
    I0.L = 0x3600; I0.H = 0x1190;
    I1.L = 0x3700; I1.H = 0x1190;
    P1 = 16;
    LSETUP (.Lscan, .Lscan_end) LC0 = P1;
.Lscan:
    MNOP || R0 = [I0++] || R2 = [I1++];
    MNOP || R1 = [I0++] || R3 = [I1++];
    R0 = R0 | R2;
    R1 = R1 | R3;
    MNOP || R2 = [I0++] || R3 = [I1++];
    R0 = R0 | R2;
    R1 = R1 | R3;
    MNOP || R2 = [I0++] || R3 = [I1++];
    R0 = R0 | R2;
    R1 = R1 | R3;
    R0 = R0 | R1;
    CC = R0 == 0;
    IF !CC JUMP .Lsound;
.Lscan_end:
    NOP;
    /* All 128 words are zero: every mix leaves its bus as it is. */
    R5 = 1;
    JUMP .Ltape;

.Lsound:
    R0 = 0;
    LC0 = R0;
    CC = R5 == 0;
    IF CC JUMP .Laux_done;
    /* aux: the gain is R5, each product shifted left 11 with saturation. */
    P0.L = 0x3600; P0.H = 0x1190;
    P2 = [FP - 0x7c];
    I1 = P2;
    P3 = 127;
    R1 = [P0++];
    R1 = R5 * R1 (T);
    R1 = R1 << 11 (S);
    R0 = [I1++];
    LSETUP (.Laux, .Laux_end) LC0 = P3;
.Laux:
    R0 = R0 + R1 (S) || R1 = [P0++] || NOP;
    R1 = R5 * R1 (T) || [P2++] = R0 || NOP;
.Laux_end:
    R1 = R1 << 11 (S) || R0 = [I1++] || NOP;
    R0 = R0 + R1 (S);
    [P2] = R0;
.Laux_done:
    R5 = 0;

    /* From here R5 is 1 when the buffer is all zero. */
.Ltape:
    P3 = 127;
    CC = R5 == 0;
    IF !CC JUMP .Ltape_done;
    CC = R6 == 0;
    IF CC JUMP .Ltape_done;
    MIX R6, 0x84ca, 0xa740
.Ltape_done:
    /* FX I: g0 = sat([FP-0xc] + [FP-8]), then + [FP-0xc] per word. */
    R6 = [FP - 0xc];
    R2 = [FP - 8];
    R2 = R6 + R2 (S);
    CC = R6 == 0;
    IF !CC JUMP .Lfx1_ramp;
    CC = R5 == 0;
    IF !CC JUMP .Lfx1_done;
    CC = R2 == 0;
    IF CC JUMP .Lfx1_done;
    MIX R2, 0x84ca, 0xa140
    JUMP .Lfx1_done;
.Lfx1_ramp:
    /* Never seen (stock stores 0 there), run as stock does, silent or not. */
    P0.L = 0x3600; P0.H = 0x1190;
    P1.L = 0xa140; P1.H = 0x84ca;
    P2 = P1;
    R1 = [P0++];
    R3 = R2 * R1 (T);
    R0 = [P1++];
    LSETUP (.Lfx1, .Lfx1_end) LC0 = P3;
.Lfx1:
    R2 = R6 + R2 (S);
    R0 = R0 + R3 (S) || R1 = [P0++] || NOP;
    R3 = R2 * R1 (T);
    [P2++] = R0;
.Lfx1_end:
    R0 = [P1++];
    R0 = R0 + R3 (S);
    [P2] = R0;
.Lfx1_done:
    [FP - 8] = R2;

    /* FX II: the gain is R7. */
    CC = R5 == 0;
    IF !CC JUMP .Lfx2_done;
    CC = R7 == 0;
    IF CC JUMP .Lfx2_done;
    MIX R7, 0x84ca, 0xa340
.Lfx2_done:

    /* Master: stock's ramp step, word for word. */
    R3 = [FP - 0x50];
    P1 = [FP - 0x34];
    P3 = [FP + 0xc];
    P0 = [SP + 0x48];
    P2 = P1;
    R2 = B [P1 + 0xa] (Z);
    CC = R2 == 0;
    R0 = [P3 ++ P0];
    R2 = 0;
    R0 = R3 * R0 (T);
    IF CC JUMP .Lramp;
    /* Snap: the gain jumps to the target, no step. */
    [P1 + 4] = R0;
    [P1] = R4;
    B [P1 + 0xa] = R4;
    JUMP .Lgain;
.Lramp:
    R1 = [P1 + 4];
    R2 = R0 - R1 (S);
    R3 = W [P1 + 8] (X);
    R2 >>>= R3;
    R0 = R1;
    [P2] = R2;
.Lgain:
    R0 = R2 + R0 (S);
    /* R0 is the first word's gain, R2 the step. */
    CC = R5 == 0;
    IF !CC JUMP .Lmaster_skip;
    CC = R0 == 0;
    IF !CC JUMP .Lmaster;
    CC = R2 == 0;
    IF CC JUMP .Lmaster_done;
.Lmaster:
    P0 = [SP + 0x58];
    I1 = P0;
    I0.L = 0x3600; I0.H = 0x1190;
    P2 = 127;
    R1 = [I0++];
    R1 = R0 * R1 (T);
    LSETUP (.Lmix, .Lmix_end) LC0 = P2;
.Lmix:
    R0 = R2 + R0 (S) || R3 = [I1++] || NOP;
    R1 = R3 + R1 (S) || R3 = [I0++] || NOP;
.Lmix_end:
    R1 = R0 * R3 (T) || [P0++] = R1 || NOP;
    R3 = [I1];
    R1 = R3 + R1 (S);
    [P0] = R1;
    JUMP .Lmaster_done;
.Lmaster_skip:
    /* Nothing to add: only the gain moves, 127 more steps. */
    CC = R2 == 0;
    IF CC JUMP .Lmaster_done;
    P2 = 127;
    LSETUP (.Lstep, .Lstep) LC0 = P2;
.Lstep:
    R0 = R2 + R0 (S);
.Lmaster_done:
    P2 = [FP - 0x34];
    [P2 + 4] = R0;
    P1.L = 0x1182; P1.H = 0x8d51;
    JUMP (P1);
.size sdk_track_mix, .-sdk_track_mix
