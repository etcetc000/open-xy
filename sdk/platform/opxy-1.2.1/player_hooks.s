/* Stock note-player hooks whose register contract C++ cannot express. The
 * decisions are C++ (players.cpp); these stubs only save state, call it and
 * return its answer or replay the displaced stock instructions.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_player_build
.extern sdk_player_keep
.extern sdk_player_name
.extern sdk_player_id
.extern stock_player_factory_tail
.extern stock_player_load_tail
.extern stock_player_load_kept
.extern stock_player_name_tail
.extern stock_player_id_tail
.extern sdk_player_turn

/* Inside Maestro's turn frame, replacing its step-count call and argument
 * moves: R5 = index, R2 = event. Owned players return through the existing
 * epilogue; others replay the moves and tail-call the stock step-count helper.
 * Both GUI destinations are outside relative branch range. P0 is dead across
 * this site and the stock epilogue. No player State is accessed on this role. */
.global extensions_player_turn
.type extensions_player_turn, STT_FUNC
extensions_player_turn:
    HOOK_SAVE
    R0 = R5;
    R1 = R2;
    CALL sdk_player_turn;
    CC = R0 == 0;
    IF CC JUMP .Lturn_stock;
    HOOK_RESTORE
    P0.H = 0x8c08;
    P0.L = 0x4d46;
    JUMP (P0);
.Lturn_stock:
    HOOK_RESTORE
    R0 = R5;
    R3 = R1;
    P0.H = 0x8c08;
    P0.L = 0x4afc;
    JUMP (P0);
.size extensions_player_turn, .-extensions_player_turn

/* Entry of stock's player factory (R0 = track, R1 = type, R2 = flag). An
 * owned type returns its object or null; others continue stock. */
.global extensions_player_build
.type extensions_player_build, STT_FUNC
extensions_player_build:
    HOOK_SAVE
    CALL sdk_player_build;
    CC = R0 == -1;
    IF CC JUMP .Lfactory_stock;
    HOOK_RESULT R0
    HOOK_RESTORE
    RTS;
.Lfactory_stock:
    HOOK_RESTORE
    LINK 0;
    JUMP.L stock_player_factory_tail;
.size extensions_player_build, .-extensions_player_build

/* Stock's project-load check of a pattern's player type (P1 = pattern, the
 * type at +0x34) replaces 1 and anything from 5 with Arpeggio. Owned types
 * skip it; others replay its first two instructions and continue. */
.global extensions_player_load
.type extensions_player_load, STT_FUNC
extensions_player_load:
    R0 = [P1 + 0x34];
    HOOK_SAVE
    CALL sdk_player_keep;
    HOOK_RESULT R0
    HOOK_RESTORE
    CC = R0 == 0;
    IF CC JUMP .Lload_stock;
    JUMP.L stock_player_load_kept;
.Lload_stock:
    R0 = [P1 + 0x34];
    CC = R0 < 5 (IU);
    JUMP.L stock_player_load_tail;
.size extensions_player_load, .-extensions_player_load

/* Entry of stock's type-to-name conversion (R0 = type). */
.global extensions_player_name
.type extensions_player_name, STT_FUNC
extensions_player_name:
    HOOK_SAVE
    CALL sdk_player_name;
    CC = R0 == 0;
    IF CC JUMP .Lname_stock;
    HOOK_RESULT R0
    HOOK_RESTORE
    RTS;
.Lname_stock:
    HOOK_RESTORE
    LINK 0x14;
    JUMP.L stock_player_name_tail;
.size extensions_player_name, .-extensions_player_name

/* Entry of stock's name-to-type conversion (R0 = stock string). */
.global extensions_player_id
.type extensions_player_id, STT_FUNC
extensions_player_id:
    HOOK_SAVE
    CALL sdk_player_id;
    CC = R0 == -1;
    IF CC JUMP .Lid_stock;
    HOOK_RESULT R0
    HOOK_RESTORE
    RTS;
.Lid_stock:
    HOOK_RESTORE
    LINK 0;
    JUMP.L stock_player_id_tail;
.size extensions_player_id, .-extensions_player_id
