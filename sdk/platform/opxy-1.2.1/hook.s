/* The one register save and restore for hooks entered from inside stock
 * functions (hook.inc). The stub's LINK keeps its RETS; these routines only
 * move the stack pointer, so they push and pop on the stub's stack. Saved:
 * everything the C ABI lets compiled C++ clobber. That is R0-R3, P0-P2, ASTAT
 * (CC), the DAG registers GCC uses as scratch (I, M, B), both hardware loops
 * and both accumulators. L0-L3 are saved and cleared, since compiled code
 * assumes linear addressing. The saved R0 ends at [SP + 12], above the
 * 12-byte outgoing argument area.
 */
.section .text
.align 2

.global sdk_hook_save
.type sdk_hook_save, STT_FUNC
sdk_hook_save:
    [--SP] = ASTAT;
    [--SP] = A0.X; [--SP] = A0.W; [--SP] = A1.X; [--SP] = A1.W;
    [--SP] = LC0; [--SP] = LT0; [--SP] = LB0;
    [--SP] = LC1; [--SP] = LT1; [--SP] = LB1;
    [--SP] = I0; [--SP] = I1; [--SP] = I2; [--SP] = I3;
    [--SP] = M0; [--SP] = M1; [--SP] = M2; [--SP] = M3;
    [--SP] = B0; [--SP] = B1; [--SP] = B2; [--SP] = B3;
    [--SP] = L0; [--SP] = L1; [--SP] = L2; [--SP] = L3;
    L0 = 0; L1 = 0; L2 = 0; L3 = 0;
    [--SP] = P2; [--SP] = P1; [--SP] = P0;
    [--SP] = R3; [--SP] = R2; [--SP] = R1; [--SP] = R0;
    SP += -12;
    RTS;
.size sdk_hook_save, .-sdk_hook_save

/* Loop bounds are restored before their counters. */
.global sdk_hook_restore
.type sdk_hook_restore, STT_FUNC
sdk_hook_restore:
    SP += 12;
    R0 = [SP++]; R1 = [SP++]; R2 = [SP++]; R3 = [SP++];
    P0 = [SP++]; P1 = [SP++]; P2 = [SP++];
    L3 = [SP++]; L2 = [SP++]; L1 = [SP++]; L0 = [SP++];
    B3 = [SP++]; B2 = [SP++]; B1 = [SP++]; B0 = [SP++];
    M3 = [SP++]; M2 = [SP++]; M1 = [SP++]; M0 = [SP++];
    I3 = [SP++]; I2 = [SP++]; I1 = [SP++]; I0 = [SP++];
    LB1 = [SP++]; LT1 = [SP++]; LC1 = [SP++];
    LB0 = [SP++]; LT0 = [SP++]; LC0 = [SP++];
    A1.W = [SP++]; A1.X = [SP++]; A0.W = [SP++]; A0.X = [SP++];
    ASTAT = [SP++];
    RTS;
.size sdk_hook_restore, .-sdk_hook_restore
