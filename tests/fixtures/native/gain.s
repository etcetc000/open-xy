/* probesources_native_gain on the device; native/gain.hpp holds the C reference
 * that defines it, gain_reference.
 *
 * void probesources_native_gain(i32* samples, unsigned count, i32 gain), gain
 * 0..32767: each sample becomes its high half times gain, doubled.
 * A leaf: no stack. Clobbers R0, P0, P1, LC0/LT0/LB0, ASTAT.
 */
.section .text
.align 4
.global _probesources_native_gain
.type _probesources_native_gain, STT_FUNC
_probesources_native_gain:
    CC = R1 == 0;
    IF CC JUMP .Ldone;                /* LC0 = 0 would still run the body once */
    P0 = R0;
    P1 = R1;
    LSETUP (.Lstart, .Lend) LC0 = P1;
.Lstart:
    R0 = [P0];
    R0 = R0.H * R2.L (IS);
    R0 <<= 1;
.Lend:
    [P0++] = R0;
.Ldone:
    RTS;
.size _probesources_native_gain, .-_probesources_native_gain
