/* The stock GUI compositor's loop advance at 8d5214c2, R7 += 1; R0 = 81;
 * CC = R7 < R0, before IF CC JUMP to the loop head, which paints page R7 from
 * the slot P4 points to and post-increments P4. R7 and P4 are callee-saved, so
 * they are passed to C++ (screen_order.cpp.in) in memory; [P5] is the page
 * stock selected for this frame. Returns the loop condition in CC; R0 is dead.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_screen_next

.global sdk_screen_advance
.type sdk_screen_advance, STT_FUNC
sdk_screen_advance:
    [--SP] = P4;
    [--SP] = R7;
    HOOK_SAVE
    R0 = FP;
    R0 += 8;            /* ScreenCursor {R7, P4}, above the saved FP and RETS */
    R1 = [P5];
    CALL sdk_screen_next;
    HOOK_RESULT R0
    HOOK_RESTORE
    R7 = [SP++];
    P4 = [SP++];
    CC = R0 == 1;
    RTS;
.size sdk_screen_advance, .-sdk_screen_advance

/* 8d53b636: the send page's value-text CALL 0x8d6011f0 (R0 = widget, R1 =
 * text), inside the loop over positions. R7 holds the position and P5 the
 * parameter vector, whose first field [P5 + 4] points to its Q31 words (stock
 * reads word i there and prints it as 00-99); R7 and P5 are callee-saved, so
 * they are intact here. Pass the position and its word to C++
 * (synth_controls.cpp), which makes the call itself. */
.extern sdk_fx_value_text
.global sdk_fx_value
.type sdk_fx_value, STT_FUNC
sdk_fx_value:
    LINK 0;
    SP += -16;          /* argument area and the fourth argument */
    P2 = R7;
    P1 = [P5 + 4];
    P1 = P1 + (P2 << 2);
    R3 = [P1];
    [SP + 12] = R3;
    R2 = R7;
    CALL sdk_fx_value_text;
    SP += 16;
    UNLINK;
    RTS;
.size sdk_fx_value, .-sdk_fx_value
