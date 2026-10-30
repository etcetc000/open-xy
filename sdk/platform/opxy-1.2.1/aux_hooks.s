/* Aux track hooks in the aux controller (aux_tracks.cpp). Each replaces eight
 * bytes with an absolute CALL into a stub here; no stock branch lands inside
 * any site's eight bytes.
 */
.section .text
.align 2

/* 8c08fd30: unshifted key dispatch, after LINK and saved R7/P5 + 12 bytes
 * of argument space. Replace complete instructions (12 bytes), before any
 * jump-table branch. R0 is the controller, R1/P0 the key event. Only a held
 * track's page keys and machine-page clicks are consumed. */
.extern sdk_aux_key
.global sdk_aux_key_hook
.type sdk_aux_key_hook, STT_FUNC
sdk_aux_key_hook:
    LINK 0;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = P0;
    SP += -12;
    CALL sdk_aux_key;
    SP += 12;
    R3 = R0;
    P0 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    UNLINK;
    CC = R3 == 0;
    IF CC JUMP 1f;
    SP += 12;
    (R7:7, P5:5) = [SP++];
    UNLINK;
    R0 = R3;
    R0 += -1;
    RTS;
1:
    P2 = 50;
    R2 = R0;
    P1 = [P0];
    R7 = R0;
    RTS;
.size sdk_aux_key_hook, .-sdk_aux_key_hook

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

/* 8c08f610: Shift + track key on an aux track reaches 8c08f60c(controller,
 * track), which opens the effect list for the sends and does nothing else.
 * Just after its LINK, `[--SP] = (R7:4); R4 = R1; R7 = R1; R1 = 14` fills
 * eight bytes. The stub asks sdk_aux_shift_track(controller, track): when it
 * opened a list (1), it leaves the method as its UNLINK; RTS would; otherwise it
 * makes the replaced instructions and returns into stock at 8c08f618.
 */
.extern sdk_aux_shift_track
.global sdk_aux_shift_track_hook
.type sdk_aux_shift_track_hook, STT_FUNC
sdk_aux_shift_track_hook:
    LINK 0;
    [--SP] = R0;
    [--SP] = R1;
    SP += -12;
    CALL sdk_aux_shift_track;
    SP += 12;
    R2 = R0;
    R1 = [SP++];
    R0 = [SP++];
    UNLINK;
    CC = R2 == 0;
    IF CC JUMP 1f;
    UNLINK;
    RTS;
1:
    [--SP] = (R7:4);
    R4 = R1;
    R7 = R1;
    R1 = 14;
    RTS;
.size sdk_aux_shift_track_hook, .-sdk_aux_shift_track_hook

/* 8c08f540: the aux page method 8c08f53c, just after its LINK 0xc, loads
 * P1 = 840a0534 (the selected track) over eight bytes. sdk_aux_page() answers
 * the page a machine shows there, or -1: then the stub makes the replaced load
 * and returns into stock; otherwise it leaves the method with that page, as
 * the method's own UNLINK; RTS would. R0-R3 and P0-P2 are dead at the site.
 */
.extern sdk_aux_page
.global sdk_aux_page_hook
.type sdk_aux_page_hook, STT_FUNC
sdk_aux_page_hook:
    LINK 0;
    SP += -12;
    CALL sdk_aux_page;
    SP += 12;
    UNLINK;
    CC = R0 < 0;
    IF CC JUMP 1f;
    UNLINK;
    RTS;
1:
    P1.L = 0x0534;
    P1.H = 0x840a;
    RTS;
.size sdk_aux_page_hook, .-sdk_aux_page_hook

/* 8d53d2a0: the aux LFO's destination label 8d53d160(track, group, slot),
 * after its LINK 0x10, [--SP] = (R7:4) and SP += -0x10, with track in R7 and
 * slot in R5, loads P2 = 84056d50 ("param1".."param4") over eight bytes for
 * group 0. sdk_aux_lfo_label(track, slot) answers the name of that control
 * of the machine holding the track, or 0: then the stub makes the replaced
 * load and returns into stock; otherwise it leaves the method with the name,
 * as the method's own epilogue would. R0-R3 and P0-P2 are dead at the site.
 */
.extern sdk_aux_lfo_label
.global sdk_aux_lfo_label_hook
.type sdk_aux_lfo_label_hook, STT_FUNC
sdk_aux_lfo_label_hook:
    LINK 0;
    SP += -12;
    R0 = R7;
    R1 = R5;
    CALL sdk_aux_lfo_label;
    SP += 12;
    UNLINK;
    CC = R0 == 0;
    IF CC JUMP 1f;
    SP += 0x10;
    (R7:4) = [SP++];
    UNLINK;
    RTS;
1:
    P2.L = 0x6d50;
    P2.H = 0x8405;
    RTS;
.size sdk_aux_lfo_label_hook, .-sdk_aux_lfo_label_hook
