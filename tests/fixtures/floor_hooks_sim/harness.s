/* Stock's 11a044e6..11a0451c (relocated: L1 code is not readable by the core) and sdk_lfo_pass, from the
 * same registers and frame; both end at lfo_return (the test points the hook's jump to 11a0451c there). */
.section .text
.align 2
stockwin:
    .incbin "lfo.bin"
    JUMP.L lfo_return;

.section .bss
.align 4
save_area: .space 16

.section .text
/* run_lfo(Ctx* c, which). Ctx: 0 R0, 4 R1, 8 R2, 12 R4, 16 R5, 20 R6, 24 R7, 28 P3, 32 P4, 36 P5, 40 FP,
 * 44 cycles; on return: 0 R0, 4 R1, 12 R4, 16 R5, 20 R6, 24 R7, 28 P3, 32 P4, 36 P5, 40 FP, 48 SP. */
.global _run_lfo
.type _run_lfo, STT_FUNC
_run_lfo:
    [--SP] = (R7:4, P5:3);
    [--SP] = FP;
    [--SP] = RETS;
    P0.L = save_area; P0.H = save_area;
    [P0] = SP;
    [P0 + 4] = R0;
    P1 = R0;
    [P0 + 12] = R1;
    R0 = [P1]; R2 = [P1 + 8]; R4 = [P1 + 12]; R5 = [P1 + 16]; R6 = [P1 + 20]; R7 = [P1 + 24];
    P3 = [P1 + 28]; P4 = [P1 + 32]; P5 = [P1 + 36]; FP = [P1 + 40];
    SP = FP; SP += -0x38;
    R3 = [P0 + 12];
    CC = R3 == 0;
    R1 = CYCLES;
    [P0 + 8] = R1;
    R1 = [P1 + 4];
    IF CC JUMP 1f;
    JUMP.L sdk_lfo_pass;
1:  JUMP.L stockwin;
.global lfo_return
lfo_return:
    R3 = CYCLES;
    P0.L = save_area; P0.H = save_area;
    R2 = [P0 + 8];
    R3 = R3 - R2;
    P1 = [P0 + 4];
    [P1] = R0; [P1 + 4] = R1; [P1 + 12] = R4; [P1 + 16] = R5; [P1 + 20] = R6; [P1 + 24] = R7;
    [P1 + 28] = P3; [P1 + 32] = P4; [P1 + 36] = P5; [P1 + 40] = FP; [P1 + 44] = R3;
    R2 = SP; [P1 + 48] = R2;
    SP = [P0];
    RETS = [SP++];
    FP = [SP++];
    (R7:4, P5:3) = [SP++];
    RTS;
.size _run_lfo, .-_run_lfo

/* call_tape(): stock's tape track 8d50fa88(84c696fc, 11b00948) with every other register from _regv:
 * 0-5 R2-R7, 6-11 P0-P5 (P0 is the call target instead), 12-15 I0-I3, 16-19 M0-M3, 20-23 B0-B3, 24-25 LC0-LC1,
 * 26-27 A0.W, A1.W, 28-29 A0.X, A1.X; L0-L3 0 as the ABI has them. */
.section .text
.global _call_tape
.type _call_tape, STT_FUNC
_call_tape:
    [--SP] = (R7:4, P5:3);
    [--SP] = FP;
    [--SP] = RETS;
    P0.L = _regv; P0.H = _regv;
    R0 = [P0 + 48]; I0 = R0; R0 = [P0 + 52]; I1 = R0; R0 = [P0 + 56]; I2 = R0; R0 = [P0 + 60]; I3 = R0;
    R0 = [P0 + 64]; M0 = R0; R0 = [P0 + 68]; M1 = R0; R0 = [P0 + 72]; M2 = R0; R0 = [P0 + 76]; M3 = R0;
    R0 = [P0 + 80]; B0 = R0; R0 = [P0 + 84]; B1 = R0; R0 = [P0 + 88]; B2 = R0; R0 = [P0 + 92]; B3 = R0;
    R0 = 0; L0 = R0; L1 = R0; L2 = R0; L3 = R0;
    R0 = [P0 + 96]; LC0 = R0; R0 = [P0 + 100]; LC1 = R0;
    R0 = [P0 + 104]; A0.W = R0; R0 = [P0 + 108]; A1.W = R0; R0 = [P0 + 112]; A0.X = R0.L; R0 = [P0 + 116]; A1.X = R0.L;
    R2 = [P0]; R3 = [P0 + 4]; R4 = [P0 + 8]; R5 = [P0 + 12]; R6 = [P0 + 16]; R7 = [P0 + 20];
    P1 = [P0 + 28]; P2 = [P0 + 32]; P3 = [P0 + 36]; P4 = [P0 + 40]; P5 = [P0 + 44];
    P0.L = 0xfa88; P0.H = 0x8d50;
    R0.L = 0x96fc; R0.H = 0x84c6;
    R1.L = 0x0948; R1.H = 0x11b0;
    SP += -12;   /* the callee's argument home slots */
    CALL (P0);
    SP += 12;
    RETS = [SP++];
    FP = [SP++];
    (R7:4, P5:3) = [SP++];
    RTS;
.size _call_tape, .-_call_tape
