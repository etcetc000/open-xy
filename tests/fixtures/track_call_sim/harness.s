/* Glue for the track call probe (harness.cpp) on the BF707 Functional-Sim. */
.section .text
.align 2
.extern stub_render

/* stub_loop(manager R0, track R1, sum R2): in place of stock's voice loop
 * 8d51c708, with its frame and its stores into the caller's 12-byte argument
 * area (8d51c708..8d51c756), read back from there before it returns
 * (8d51c7ea..8d51c7f6). The voices it adds come from stub_render, whose count
 * it returns, as the loop returns its voices'. */
.global stub_loop
.type stub_loop, STT_FUNC
stub_loop:
    P1 = R1;
    LINK 0;
    [--SP] = (R7:4, P5:3);
    SP += -12;
    [SP + 0x34] = R2;
    [SP + 0x38] = P1;
    P2 = R0;
    [SP + 0x30] = P2;
    R0 = R1;
    R1 = R2;
    CALL stub_render;
    R1 = [SP + 0x34];
    P1 = [SP + 0x38];
    P2 = [SP + 0x30];
    R2 = P1;
    R3 = P2;
    SP += 12;
    (R7:4, P5:3) = [SP++];
    UNLINK;
    RTS;
.size stub_loop, .-stub_loop

.section .bss
.align 4
save_area:
    .space 8

.section .text
.align 2
/* run_call(Call* c, which): R0..R2 from c (manager, track, sum), R4..R7,
 * P3..P5 and FP set to c's markers, then CALL stub_loop (which 0, as the
 * engine calls stock's loop at 8d510bea) or sdk_track_call (which 1, the shared
 * handler). Stores back R0, those registers, SP's change and the cycles. */
.global run_call
.type run_call, STT_FUNC
run_call:
    [--SP] = (R7:4, P5:3);
    [--SP] = FP;
    [--SP] = RETS;
    P0.L = save_area; P0.H = save_area;
    [P0] = SP;
    [P0 + 4] = R0;
    P1 = R0;
    CC = R1 == 0;
    R4 = [P1 + 12];
    R5 = [P1 + 16];
    R6 = [P1 + 20];
    R7 = [P1 + 24];
    P3 = [P1 + 28];
    P4 = [P1 + 32];
    P5 = [P1 + 36];
    FP = [P1 + 40];
    R0 = [P1 + 0];
    R2 = [P1 + 8];
    R1 = [P1 + 4];
    SP += -12;
    IF CC JUMP .Lstock;
    R3 = CYCLES;
    [P0 + 8] = R3;
    CALL sdk_track_call;
    JUMP.S .Ldone;
.Lstock:
    R3 = CYCLES;
    [P0 + 8] = R3;
    CALL stub_loop;
.Ldone:
    R3 = CYCLES;
    SP += 12;
    P0.L = save_area; P0.H = save_area;
    R2 = [P0 + 8];
    R3 = R3 - R2;
    R2 = [P0];
    R1 = SP;
    R2 = R1 - R2;
    P1 = [P0 + 4];
    [P1 + 44] = R0;
    [P1 + 48] = R4;
    [P1 + 52] = R5;
    [P1 + 56] = R6;
    [P1 + 60] = R7;
    [P1 + 64] = P3;
    [P1 + 68] = P4;
    [P1 + 72] = P5;
    [P1 + 76] = FP;
    [P1 + 80] = R2;
    [P1 + 84] = R3;
    SP = [P0];
    RETS = [SP++];
    FP = [SP++];
    (R7:4, P5:3) = [SP++];
    RTS;
.size run_call, .-run_call
