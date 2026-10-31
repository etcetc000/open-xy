/* The auxiliary controller (vtable 0803fb6c) for aux modules (aux_modules.cpp).
 * Stock calls its methods through the vtable, too far from the module for a
 * relative branch, so each hook replaces an eight-byte instruction group early
 * in the method with an absolute call. A stub either returns from the stock
 * method itself, through the method's own epilogue, or replays the group and
 * returns into it.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_aux_screen
.extern sdk_aux_turn
.extern sdk_aux_preset_key
.extern sdk_aux_engine_key

/* 8c08f540: method 11, the page for the selected aux track, after its LINK
 * 0xc, in place of P1 = 0x840a0534 (the selected track). A nonzero page from
 * C++ is the method's result. */
.global sdk_aux_screen_hook
.type sdk_aux_screen_hook, STT_FUNC
sdk_aux_screen_hook:
    HOOK_SAVE
    CALL sdk_aux_screen;
    CC = R0 == 0;
    IF CC JUMP 1f;
    HOOK_RESULT R0
    HOOK_RESTORE
    UNLINK;
    RTS;
1:
    HOOK_RESTORE
    P1.L = 0x0534;
    P1.H = 0x840a;
    RTS;
.size sdk_aux_screen_hook, .-sdk_aux_screen_hook

/* 8c08fa14: method 6, an encoder turn (R1 the encoder, R2 the event), after
 * LINK 0x30, [--SP] = (R7:4, P5:3) and SP += -16, in place of P5 = 0x840a0534.
 * When C++ edited a module's control, the method returns 1 through its
 * epilogue; otherwise stock's turn continues with every register intact. */
.global sdk_aux_turn_hook
.type sdk_aux_turn_hook, STT_FUNC
sdk_aux_turn_hook:
    HOOK_SAVE
    R0 = [SP + 16];     /* the saved R1 and R2 */
    R1 = [SP + 20];
    CALL sdk_aux_turn;
    CC = R0 == 0;
    IF CC JUMP 1f;
    HOOK_RESULT R0
    HOOK_RESTORE
    SP += 16;
    (R7:4, P5:3) = [SP++];
    UNLINK;
    RTS;
1:
    HOOK_RESTORE
    P5.L = 0x0534;
    P5.H = 0x840a;
    RTS;
.size sdk_aux_turn_hook, .-sdk_aux_turn_hook

/* 8c08f624: stock's selector for Shift and an aux track's key
 * (8c08f60c(controller, track)) leaving for any track but a send, in place of
 * its epilogue SP += 0x14; (R7:4) = [SP++]; UNLINK. R4 holds the track. The
 * stub completes the epilogue and returns to the selector's caller. */
.global sdk_aux_preset_hook
.type sdk_aux_preset_hook, STT_FUNC
sdk_aux_preset_hook:
    HOOK_SAVE
    R0 = R4;
    CALL sdk_aux_preset_key;
    HOOK_RESTORE
    SP += 0x14;
    (R7:4) = [SP++];
    UNLINK;
    RTS;
.size sdk_aux_preset_hook, .-sdk_aux_preset_hook

/* 8c08f6ec: the controller's Shift-key method (8c08f6b0, its method 4) for
 * M1 (key code 6), in place of UNLINK; R0 = 1; RTS, which swallowed the key.
 * The stub calls C++, then completes the method as stock did: it returns 1,
 * the key handled. */
.global sdk_aux_engine_hook
.type sdk_aux_engine_hook, STT_FUNC
sdk_aux_engine_hook:
    HOOK_SAVE
    CALL sdk_aux_engine_key;
    HOOK_RESTORE
    UNLINK;
    R0 = 1;
    RTS;
.size sdk_aux_engine_hook, .-sdk_aux_engine_hook

/* The stock module beneath a module, quietened. Both hooks replace a
 * function's first instruction with a jump here (call: false). When C++ says
 * to drop the call, the stub returns to the function's caller at once;
 * otherwise it replays the replaced LINK and continues in stock. P1 carries
 * the continuation: it is no argument, and stock writes it before reading it. */
.extern sdk_aux_silent
.extern sdk_aux_quiet

/* 8d595738: stock's note start for a track (R1), sequenced or played, with
 * LINK 0x4c first. No caller reads a result. */
.global sdk_aux_note_hook
.type sdk_aux_note_hook, STT_FUNC
sdk_aux_note_hook:
    HOOK_SAVE
    R0 = [SP + 16];     /* the saved R1 */
    CALL sdk_aux_silent;
    CC = R0 == 0;
    IF CC JUMP 1f;
    HOOK_RESTORE
    RTS;
1:
    HOOK_RESTORE
    LINK 0x4c;
    P1.L = 0x573c;
    P1.H = 0x8d59;
    JUMP (P1);
.size sdk_aux_note_hook, .-sdk_aux_note_hook

/* 8d58a730: the MIDI track's CC for a parameter it publishes (R0 the track,
 * R1 the parameter, R2 the value), with LINK 0 first. It returns nothing. */
.global sdk_aux_midi_hook
.type sdk_aux_midi_hook, STT_FUNC
sdk_aux_midi_hook:
    HOOK_SAVE
    R0 = [SP + 12];     /* the saved R0 and R1 */
    R1 = [SP + 16];
    CALL sdk_aux_quiet;
    CC = R0 == 0;
    IF CC JUMP 1f;
    HOOK_RESTORE
    RTS;
1:
    HOOK_RESTORE
    LINK 0;
    P1.L = 0xa734;
    P1.H = 0x8d58;
    JUMP (P1);
.size sdk_aux_midi_hook, .-sdk_aux_midi_hook
