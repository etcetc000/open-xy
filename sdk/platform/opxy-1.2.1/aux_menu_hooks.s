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

