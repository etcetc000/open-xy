.section .text
.align 2
/* 8c0b5c14: the key layer's encoder turn (8c0b5c0c), after LINK 0, saved
 * R7:5 and 12 bytes of argument space. R0/R1 are encoder index/turn record.
 * Replaces the complete 8-byte P1 = 08023c48 load. When sdk_punch_turn takes
 * the turn the stub leaves 8c0b5c0c through its own epilogue (its UNLINK
 * restores the caller's RETS); otherwise it replays the load with R0/R1
 * preserved. */
.extern sdk_punch_turn
.global sdk_punch_turn_hook
.type sdk_punch_turn_hook, STT_FUNC
sdk_punch_turn_hook:
    LINK 0;
    [--SP] = R0;
    [--SP] = R1;
    SP += -12;
    CALL sdk_punch_turn;
    SP += 12;
    R2 = R0;
    R1 = [SP++];
    R0 = [SP++];
    UNLINK;
    CC = R2 == 0;
    IF CC JUMP 1f;
    SP += 12;
    (R7:5) = [SP++];
    UNLINK;
    RTS;
1:
    P1.L = 0x3c48;
    P1.H = 0x0802;
    RTS;
.size sdk_punch_turn_hook, .-sdk_punch_turn_hook
