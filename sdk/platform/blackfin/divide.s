/* Integer division helpers for compiled machine code, which GCC calls by
 * their libgcc names; no libgcc is linked.
 *
 * libgcc's Blackfin __udivsi3 always runs 32 shift/subtract steps (~166
 * instructions). Divisors below 32768 (nearly every division in machine code:
 * digits, sizes, small counts) use the chip's divide steps: DIVS then 16 DIVQ
 * give a 16-bit quotient in 17 single-cycle steps (opxy/divide.hpp), once when
 * the quotient fits 16 bits and twice (high half, then the remainder with the
 * low half) when it does not; the remainder is the dividend less quotient x
 * divisor. Larger divisors align the divisor with the dividend's top bit and
 * run only quotient-width shift/subtract steps. Results are the exact C
 * quotient/remainder, the same as before for every operand.
 *
 * ABI (as libgcc): R0 = dividend, R1 = divisor -> R0 = quotient. __udivsi3
 * also leaves the remainder in R3. Clobbers R1..R3, P0 (and R4 is saved in
 * the signed wrappers), CC, ASTAT, LC0/LT0/LB0. Division by zero is undefined
 * in C; it returns quotient 0xffffffff and remainder = dividend here.
 */
.section .text
.align 2
.global ___udivsi3
.type ___udivsi3, STT_FUNC
___udivsi3:
    CC = R1 == 0;
    IF CC JUMP .Lby_zero;
    CC = R1 <= R0 (IU);
    IF !CC JUMP .Lsmall;
    /* Divisor below 32768: the divide steps. */
    R2 = 0x7fff (Z);
    CC = R1 <= R2 (IU);
    IF !CC JUMP .Lwide;
    R3 = R0;
    R2 = R1 << 16;
    CC = R3 < R2 (IU);
    IF !CC JUMP .Ltwo_halves;
    DIVS (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    R0 = R0.L (Z);
    R2 = R0;
    R2 *= R1;
    R3 = R3 - R2;
    RTS;
.Ltwo_halves:
    /* High half: (n >> 16) / d, below 65536 * d. */
    R2 = R0 >> 16;
    DIVS (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    DIVQ (R2, R1);
    R2 = R2.L (Z);
    P0 = R2;
    R3 = R2;
    R3 *= R1;
    R2 = R0 >> 16;
    R2 = R2 - R3;
    R2 <<= 16;
    R0 = R0.L (Z);
    R0 = R0 | R2;
    /* Low half: (remainder << 16 | n & 0xffff) / d, below 65536 * d. */
    R3 = R0;
    DIVS (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    DIVQ (R0, R1);
    R0 = R0.L (Z);
    R2 = R0;
    R2 *= R1;
    R3 = R3 - R2;
    R2 = P0;
    R2 <<= 16;
    R0 = R0 | R2;
    RTS;
.Lwide:
    /* steps = clz(divisor) - clz(dividend) + 1, in 1..32. For x != 0,
     * clz(x) = x < 0x80000000 ? SIGNBITS(x) + 1 : 0. */
    R2 = 0;
    CC = R0 < 0;
    IF CC JUMP .Ldividend_top;
    R2.L = SIGNBITS R0;
    R2 = R2.L (Z);
    R2 += 1;
.Ldividend_top:
    R3 = 0;
    CC = R1 < 0;
    IF CC JUMP .Ldivisor_top;
    R3.L = SIGNBITS R1;
    R3 = R3.L (Z);
    R3 += 1;
.Ldivisor_top:
    R3 = R3 - R2;
    R1 <<= R3;
    R3 += 1;
    P0 = R3;
    R3 = 0;
    LSETUP (.Lstep, .Lstep_end) LC0 = P0;
.Lstep:
    CC = R1 <= R0 (IU);
    R2 = R0 - R1;
    IF CC R0 = R2;
    R3 = ROT R3 BY 1;
.Lstep_end:
    R1 >>= 1;
    R2 = R0;
    R0 = R3;
    R3 = R2;
    RTS;
.Lsmall:
    R3 = R0;
    R0 = 0;
    RTS;
.Lby_zero:
    R3 = R0;
    R0 = -1;
    RTS;
.size ___udivsi3, .-___udivsi3

.global ___umodsi3
.type ___umodsi3, STT_FUNC
___umodsi3:
    [--SP] = RETS;
    CALL ___udivsi3;
    R0 = R3;
    RETS = [SP++];
    RTS;
.size ___umodsi3, .-___umodsi3

/* Signed: |a| / |b| unsigned, quotient negated when signs differ; the
 * remainder takes the dividend's sign (C truncation semantics). */
.global ___divsi3
.type ___divsi3, STT_FUNC
___divsi3:
    [--SP] = RETS;
    [--SP] = R4;
    R4 = R0 ^ R1;
    R2 = -R0;
    CC = R0 < 0;
    IF CC R0 = R2;
    R2 = -R1;
    CC = R1 < 0;
    IF CC R1 = R2;
    CALL ___udivsi3;
    R2 = -R0;
    CC = R4 < 0;
    IF CC R0 = R2;
    R4 = [SP++];
    RETS = [SP++];
    RTS;
.size ___divsi3, .-___divsi3

.global ___modsi3
.type ___modsi3, STT_FUNC
___modsi3:
    [--SP] = RETS;
    [--SP] = R4;
    R4 = R0;
    R2 = -R0;
    CC = R0 < 0;
    IF CC R0 = R2;
    R2 = -R1;
    CC = R1 < 0;
    IF CC R1 = R2;
    CALL ___udivsi3;
    R0 = R3;
    R2 = -R0;
    CC = R4 < 0;
    IF CC R0 = R2;
    R4 = [SP++];
    RETS = [SP++];
    RTS;
.size ___modsi3, .-___modsi3
