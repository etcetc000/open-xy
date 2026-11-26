/* Exact savings in stock work that runs every block, whatever the project plays
 * (docs/abi-1.2.1.md "LFO pass check", "Idle tape"): sdk_lfo_pass, the LFO
 * evaluator's pass decision without its two calls, and sdk_tape_play /
 * sdk_tape_tail, the tape track's playback and mixing while the tape is
 * silent. Each produces exactly what stock's own instructions produce: the
 * same registers live after it, the same memory, the same state. */
.section .text
.align 2

/* LFO pass check.
 *
 * Stock's LFO evaluator 11a0446e(state, track, params, pass) (L1) runs for
 * each track (passes 2, 4, 3) and, while the track's LFO is on, for each of
 * its voices (passes 0 and 1). Before anything else it works out which pass
 * the LFO belongs to with two helper calls in DDR, 8d504ad4 and 8d504a50, and
 * returns 0 at once unless the asked pass is that one (or 3): about half the
 * calls stop there. Both helpers are pure:
 *
 *   8d504ad4(type, p60, p68, [SP+0xc] p6c, [SP+0x10] flag pointer):
 *     type 0 or 6: 3, flag untouched.
 *     type 3: flag = (field(p60) == 2); f = field(p68), f == 3 ? field(p6c) + 3 : f.
 *     else (1, 2, 4, 5, 7.., unsigned): x = (p68 >>> 8) * 6;
 *       flag = (bit 23 of x == 0); x >>> 24.
 *     field(w) = EXTRACT(w >>> 8, 0x1509) (X): 9 bits from bit 21, signed.
 *   8d504a50(-, r, dest, [SP+0xc] engine, [SP+0x10] flag, [SP+0x14] track):
 *     r >= 3 (signed): 1.
 *     r == 0, dest == 3, engine 6, 0x20 or 0x12: 2.
 *     r == 2, and engine 3 or 4 (8d5b79f0) or [84042700 + 4 track] == 0x12: 4.
 *     else (flag == 0) << 1.
 *
 * The hook replaces 11a044e6 (ABSCALL 8d504ad4, 8 bytes) with an absolute
 * jump here; this code does the same arithmetic inline, with stock's own
 * instructions, and the same stores the 54 bytes up to 11a0451c make between
 * and around the two calls: the flag byte at [FP+8] (when 8d504ad4 writes
 * it), [FP+0x10] = the first result, [SP+0xc] = engine, [SP+0x10] = flag,
 * [SP+0x14] = track, R7 = 0x1509 (from [SP+0x34]), R1 = the first result,
 * R0 = the pass. It returns to 11a0451c. R4-R6, P3-P5, FP and SP are as
 * stock's callees leave them (untouched); R2, R3 and P0-P2 are dead there
 * (the code after a call writes caller-saved registers before reading them).
 *
 * At entry (from 11a044d6..11a044e2): R0 = R5 = the LFO type, R1 = [P5+0x60],
 * R2 = [P5+0x68], [SP+0xc] = [P5+0x6c], [SP+0x10] = FP+8, B[FP+8] = 0,
 * R7 = the destination word, R4 = track, [FP+0x10] = the track's engine,
 * [SP+0x34] = 0x1509. */
.global sdk_lfo_pass
.type sdk_lfo_pass, STT_FUNC
sdk_lfo_pass:
    /* 8d504ad4 */
    R3 = [SP + 0xc];
    P1 = [SP + 0x10];
    R1 >>>= 8;
    CC = R0 < 3 (IU);
    IF CC JUMP 1f;
    CC = R0 == 3;
    IF CC JUMP 3f;
    R3 = 6;
    CC = R0 == R3;
    IF !CC JUMP 2f;
5:  R0 = 3;
    JUMP 4f;
1:  CC = R0 == 0;
    IF CC JUMP 5b;
2:  R1 = R2 >>> 8;
    R2 = 6;
    R0 = 0x1701 (X);
    R1 *= R2;
    R0 = EXTRACT (R1, R0.L) (Z);
    CC = R0 == 0;
    R0 = CC;
    B[P1] = R0;
    R0 = R1 >>> 24;
    JUMP 4f;
3:  R0 = 0x1509 (X);
    R1 = EXTRACT (R1, R0.L) (X);
    R3 >>>= 8;
    R2 >>>= 8;
    CC = R1 == 2;
    R1 = EXTRACT (R3, R0.L) (X);
    R0 = EXTRACT (R2, R0.L) (X);
    R2 = CC;
    B[P1] = R2;
    CC = R0 == 3;
    R1 += 3;
    IF CC R0 = R1;
4:  /* 11a044ee..11a04513: the second call's arguments, as stock stores them. */
    R3 = R7 >>> 8;
    R1 = [FP + 0x10];
    R7 = [SP + 0x34];
    R2 = EXTRACT (R3, R7.L) (X);
    [FP + 0x10] = R0;
    R3 = B[SP + 0x40] (Z);
    [SP + 0xc] = R1;
    [SP + 0x14] = R4;
    [SP + 0x10] = R3;
    /* 8d504a50: R0 = the first result, R1 = engine, R2 = dest, R3 = flag. */
    P0 = R1;
    R1 = R0;
    R0 = 1;
    CC = R1 < 3;
    IF !CC JUMP 9f;
    CC = R1 == 0;
    IF !CC JUMP 7f;
    CC = R2 == 3;
    IF !CC JUMP 8f;
    R2 = P0;
    R0 = 6;
    CC = R2 == R0;
    R0 = 0x20;
    IF CC JUMP 6f;
    CC = R2 == R0;
    R0 = 0x12;
    IF CC JUMP 6f;
    CC = R2 == R0;
    IF !CC JUMP 8f;
6:  R0 = 2;
    JUMP 9f;
7:  CC = R1 == 2;
    IF !CC JUMP 8f;
    R2 = P0;
    R0 = 4;
    CC = R2 == 3;          /* 8d5b79f0: engine 3 or 4 */
    IF CC JUMP 9f;
    CC = R2 == R0;
    IF CC JUMP 9f;
    P2.L = 0x2700; P2.H = 0x8404;
    P1 = R4;
    P2 = P2 + (P1 << 2);
    R2 = [P2];
    R1 = 0x12;
    CC = R2 == R1;
    IF CC JUMP 9f;
8:  CC = R3 == 0;
    R0 = CC;
    R0 <<= 1;
9:  R1 = [FP + 0x10];
    P1 = 0x11a0451c;
    JUMP (P1);
.size sdk_lfo_pass, .-sdk_lfo_pass

/* Idle tape: playback.
 *
 * Stock's tape machine 8d50374c(tape 84c6992c, input bus, out 11903600,
 * params) runs every block. After its start-up clear, unless the tape is near
 * its end, it calls 8d503548(tape, L, R, rate) (8d503878), which reads 64
 * frames of tape memory through two heads into the scratch buffers
 * [842e2a74] and [842e2a78], stepping both heads (+8, +0xc) by `rate` a frame
 * and the crossfade +0xf4 by +0xf8 a frame (saturating, floored at 0); then
 * it writes out[k] = g * scratch[k] (T), 128 words, g starting at +0xf0 and
 * stepping by (target - g) >>> 7 a frame pair, target the play gain
 * ([SP+0x38], from 8d503070) while +0x100 (playing) is set and R6 (0)
 * otherwise; it stores the last g in +0xf0, adds 0x10000 to +0x20 and returns.
 *
 * While target and g are both 0 (the tape is not playing and its fade-out
 * has finished: the normal state of a project that does not use the tape),
 * every g is 0, every output word is 0 * x (T) = 0 whatever the scratch holds,
 * and nothing reads the scratch buffers but that loop (8d503548 rewrites them
 * before it runs again). The hook replaces the call (8d503878, CALL
 * 8d503548) with a jump here: in that state it steps the heads and the
 * crossfade exactly as 8d503548 would (64 wrapping adds of `rate`; 64 times
 * xf = max(sat(xf + step), 0)), writes the 128 zero words and continues at
 * 8d503904 with R1 = 0 (stock's stores of g and +0x20, and its epilogue);
 * otherwise it makes stock's call and returns to 8d50387c. At entry R0 = R7 =
 * P5 = the tape, R1/R2 the scratch buffers, [SP+0xc] = rate, P3 = out, R6 = 0
 * unless set by stock (it is not), [SP+0x38] the play gain. */
.global sdk_tape_play
.type sdk_tape_play, STT_FUNC
sdk_tape_play:
    R3 = B[P5 + 0x100] (Z);
    CC = R3 == 0;
    R3 = R6;
    IF CC JUMP 1f;
    R3 = [SP + 0x38];
1:  CC = R3 == 0;
    IF !CC JUMP 2f;
    R3 = [P5 + 0xf0];
    CC = R3 == 0;
    IF !CC JUMP 2f;
    /* The heads and the crossfade. */
    R3 = [SP + 0xc];
    R3 <<= 6;
    R2 = [P5 + 0x8];
    R2 = R2 + R3;
    [P5 + 0x8] = R2;
    R2 = [P5 + 0xc];
    R2 = R2 + R3;
    [P5 + 0xc] = R2;
    R1 = [P5 + 0xf8];
    R2 = [P5 + 0xf4];
    R3 = 0;
    P0 = 64;
    LSETUP (3f, 4f) LC0 = P0;
3:  R2 = R2 + R1 (S);
4:  R2 = MAX (R2, R3);
    [P5 + 0xf4] = R2;
    /* The 128 output words, all 0. */
    P0 = 128;
    P1 = P3;
    LSETUP (5f, 5f) LC0 = P0;
5:  [P1++] = R3;
    /* Tell sdk_tape_tail, next in this block, that out (11903600) is all 0. */
    P1.L = sdk_tape_zeroed; P1.H = sdk_tape_zeroed;
    R1 = 1;
    B[P1] = R1;
    R1 = 0;
    P1.L = 0x3904; P1.H = 0x8d50;
    JUMP (P1);
2:  P2.L = sdk_tape_zeroed; P2.H = sdk_tape_zeroed;
    R3 = 0;
    B[P2] = R3;
    P2.L = 0x3548; P2.H = 0x8d50;
    CALL (P2);
    P1.L = 0x387c; P1.H = 0x8d50;
    JUMP (P1);
.size sdk_tape_play, .-sdk_tape_play

/* Idle tape: mixing.
 *
 * After the tape machine, stock's tape track 8d50fa88 runs the tape filter
 * 8d50f944 on 11903600 while 11b01dd5 is set, then (8d50fb06..8d50fd2a):
 *   level ramp (84c68c08 step, +4 value, +8 halfword shift, +0xa snap byte)
 *     towards params+0xa4 * 0x5b6db0de (T): snap, or step = sat(target -
 *     value) >>> shift;
 *   pan ramp (84c68c14, same layout) towards params+0xa0, likewise;
 *   the pan curve: 64 words at 11902e00, word k = value + (k + 1) step
 *     (each add saturating), the last also stored as the pan value;
 *   for each of the 64 frames: level += step (S), and the frame's two words
 *     times level and the pan law added (S) into the master buffer and, times
 *     params+0x58 / +0x5c, into FX I (84caa140) and FX II (84caa340);
 *     the last level stored back;
 *   the track meter: m = |11903600[0]| << 8 (S), follower 84ca9da0 (attack
 *     84ca9dac, release 84ca9db0) into 84ca9d9c/84ca9da0, and 8d4d666a(0x11, m).
 * Each mixed term is a (T) product with a buffer word as a factor: with an
 * all-zero buffer every term is exactly 0 and every bus add leaves the bus as
 * it was. The hook replaces 8d50fac4 (IMM32 P1 = 11b01dd5, 8 bytes) with a
 * jump here. While the filter is off, both ramps are constructed (guards
 * 84c689fc and 84c68a04 set) and the buffer is all zero, it does the ramps,
 * the pan curve, the 64 level steps and the meter with stock's instructions,
 * skips the zero products, and leaves through stock's epilogue (8d50fd22);
 * otherwise it loads P1 as the replaced instruction did and returns to
 * 8d50facc. At entry R7 = [SP+0xc] = the tape track's parameters, P3 = the
 * master buffer. */
.global sdk_tape_tail
.type sdk_tape_tail, STT_FUNC
sdk_tape_tail:
    /* Was 11903600 just zeroed by sdk_tape_play (8d50374c, called only from
     * 8d50fac0, returns straight here)? Read and clear the mark. */
    P1.L = sdk_tape_zeroed; P1.H = sdk_tape_zeroed;
    R2 = B[P1] (Z);
    R0 = 0;
    B[P1] = R0;
    P1.L = 0x1dd5; P1.H = 0x11b0;
    R0 = B[P1] (Z);
    CC = R0 == 0;
    IF !CC JUMP 9f;
    P0.L = 0x89fc; P0.H = 0x84c6;
    R0 = B[P0] (X);
    CC = R0 == 0;
    IF CC JUMP 9f;
    R0 = B[P0 + 0x8] (X);
    CC = R0 == 0;
    IF CC JUMP 9f;
    /* Is the buffer all zero? Known when marked; else its first word, then
     * every word. */
    CC = R2 == 0;
    IF !CC JUMP .Ltail_idle;
    P0.L = 0x3600; P0.H = 0x1190;
    R0 = [P0];
    CC = R0 == 0;
    IF !CC JUMP 9f;
    P2 = 128;
    R1 = 0;
    LSETUP (1f, 2f) LC0 = P2;
1:  R0 = [P0++];
2:  R1 = R1 | R0;
    CC = R1 == 0;
    IF !CC JUMP 9f;
.Ltail_idle:
    /* 8d50facc's spill of the parameters, then 8d50fb06..8d50fb4c: the level
     * ramp. */
    [SP + 0x34] = R7;
    P2 = R7;
    P5.L = 0x8c12; P5.H = 0x84c6;
    R3 = 0;
    R0 = B[P5] (Z);
    R2.L = 0xb0de; R2.H = 0x5b6d;
    CC = R0 == 0;
    P5 += -6;
    R1 = [P2 + 0xa4];
    R1 = R1 * R2 (T);
    IF CC JUMP 3f;
    P1 = P5;
    [P1--] = R1;
    R0 = R1;
    [P1] = R3;
    B[P1 + 0xa] = R3;
    JUMP 4f;
3:  P1 = P5;
    P1 += -4;
    R0 = [P1 + 0x4];
    R2 = W[P1 + 0x8] (X);
    R3 = R1 - R0 (S);
    R3 >>>= R2;
    [P1] = R3;
4:  /* 8d50fb4e..8d50fb94: the pan ramp. */
    P1.L = 0x8c1e; P1.H = 0x84c6;
    R7 = 0;
    R1 = B[P1] (Z);
    P0 = P1;
    CC = R1 == 0;
    P0 += -6;
    R2 = [P2 + 0xa0];
    IF CC JUMP 5f;
    P1 = P0;
    [P1--] = R2;
    R1 = R2;
    [P1] = R7;
    B[P1 + 0xa] = R7;
    JUMP 6f;
5:  P1 = P0;
    P1 += -4;
    R1 = [P1 + 0x4];
    R6 = W[P1 + 0x8] (X);
    R7 = R2 - R1 (S);
    R7 >>>= R6;
    [P1] = R7;
6:  /* 8d50fb96..8d50fbc0: the pan curve and the pan value. */
    I0.L = 0x2e00; I0.H = 0x1190;
    R1 = R7 + R1 (S);
    P1 = 63;
    LSETUP (7f, 7f) LC0 = P1;
7:  R1 = R7 + R1 (S) || [I0++] = R1 || NOP;
    [I0] = R1;
    [P0] = R1;
    /* The level steps, one a frame; the products are all 0. */
    P1 = 64;
    LSETUP (8f, 8f) LC0 = P1;
8:  R0 = R3 + R0 (S);
    [P5] = R0;
    /* 8d50fccc..8d50fd1e: the meter (the buffer word is 0 here, as in stock's
     * reading of it). */
    P0.L = 0x3600; P0.H = 0x1190;
    R2 = [P0];
    R2 = ABS R2;
    P1.L = 0x9c64; P1.H = 0x84ca;
    R1 = [P1 + 0x13c];
    R2 = R2 << 0x8 (S);
    R3 = R1 - R2 (S);
    CC = R1 < R2;
    R0 = [P1 + 0x148];
    R1 = [P1 + 0x14c];
    IF CC R1 = R0;
    R0 = 0x11;
    R1 = R1 * R3 (T);
    R1 = R2 + R1 (S);
    [P1 + 0x13c] = R1;
    [P1 + 0x138] = R1;
    P2.L = 0x666a; P2.H = 0x8d4d;
    CALL (P2);
    P1.L = 0xfd22; P1.H = 0x8d50;
    JUMP (P1);
9:  P1.L = 0x1dd5; P1.H = 0x11b0;
    P2.L = 0xfacc; P2.H = 0x8d50;
    JUMP (P2);
.size sdk_tape_tail, .-sdk_tape_tail

.section .bss
.align 4
/* Set by sdk_tape_play when it wrote the tape track buffer all 0 this block;
 * read and cleared by sdk_tape_tail. */
sdk_tape_zeroed:
    .space 4
