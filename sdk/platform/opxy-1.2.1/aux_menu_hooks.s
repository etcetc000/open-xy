.section .text
.align 2
/* 8c08f6ec: Shift + M1 in the aux controller's shifted-key method (method
 * table entry +0x10, 8c08f6b0), reached through its jump table for key code 6:
 * `UNLINK; R0 = 1; RTS`, which ignores the key and reports it handled. The
 * stub calls sdk_aux_shift_m1(), then leaves the method as those eight bytes
 * did.
 */
.extern sdk_aux_shift_m1
.global sdk_aux_shift_m1_hook
.type sdk_aux_shift_m1_hook, STT_FUNC
sdk_aux_shift_m1_hook:
    LINK 0;
    SP += -12;
    CALL sdk_aux_shift_m1;
    SP += 12;
    UNLINK;
    UNLINK;
    R0 = 1;
    RTS;
.size sdk_aux_shift_m1_hook, .-sdk_aux_shift_m1_hook

/* 8c08fa14: aux encoder turn, after LINK 48, saved R7:4/P5:3 and 16 bytes
 * of argument space. R0/R1/R2 are controller/encoder/event. Replaces the
 * complete 8-byte P5 load; stock fallback preserves all three arguments. */
.extern sdk_aux_turn
.global sdk_aux_turn_hook
.type sdk_aux_turn_hook, STT_FUNC
sdk_aux_turn_hook:
    LINK 0;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = R2;
    SP += -12;
    CALL sdk_aux_turn;
    SP += 12;
    R3 = R0;
    R2 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    UNLINK;
    CC = R3 == 0;
    IF CC JUMP 1f;
    SP += 16;
    (R7:4, P5:3) = [SP++];
    UNLINK;
    RTS;
1:
    P5.L = 0x0534;
    P5.H = 0x840a;
    RTS;
.size sdk_aux_turn_hook, .-sdk_aux_turn_hook
