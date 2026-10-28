/* Stock sample dispatch hooks whose register contract C++ cannot express. The
 * decisions are C++ (sample_machines.cpp, sample_controls_stock.cpp); these
 * stubs only save state, call it and replay or deliver what stock expects.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_sample_alias
.extern sdk_sample_note_route
.extern sdk_sample_key
.extern stock_sample_classify_tail
.extern stock_sample_key_tail

/* Entry of stock's sample predicate (R0 = engine). Replays the two displaced
 * instructions with the aliased engine, then continues stock. */
.global extensions_sample_classify
.type extensions_sample_classify, STT_FUNC
extensions_sample_classify:
    HOOK_SAVE
    CALL sdk_sample_alias;
    HOOK_RESULT R0
    HOOK_RESTORE
    CC = R0 < 3 (IU);
    LINK 0;
    JUMP.L stock_sample_classify_tail;
.size extensions_sample_classify, .-extensions_sample_classify

/* Replaces R5 = 30; CC = R7 == R5 in stock's note-region dispatcher (R7 =
 * engine; stock's frame restores R7 on return). sdk_sample_note_route decides:
 * kStockRoute (0) runs stock's test; otherwise the owned engine selects the
 * multisampler payload (R7 = 30) while the track and factory keep the real
 * engine, and kOwnedDrumPads (2) also skips the nearest-region test (CC = 0). */
.global extensions_sample_note_region
.type extensions_sample_note_region, STT_FUNC
extensions_sample_note_region:
    HOOK_SAVE
    R0 = R7;
    CALL sdk_sample_note_route;
    R5 = R0;
    HOOK_RESTORE
    CC = R5 == 0;
    IF CC JUMP .Lnote_nearest;
    R7 = 30;
    CC = R5 == 1;
    IF CC JUMP .Lnote_nearest;
    R5 = 30;
    CC = R5 == 0;
    RTS;
.Lnote_nearest:
    R5 = 30;
    CC = R7 == R5;
    RTS;
.size extensions_sample_note_region, .-extensions_sample_note_region

/* Entry of stock's region key lookup (R0 = track, R1 = key). sdk_sample_key
 * returns zero for singleSample, a drum page's canonical pad, or -1 for stock
 * with every register as it arrived. */
.global extensions_sample_key
.type extensions_sample_key, STT_FUNC
extensions_sample_key:
    HOOK_SAVE
    CALL sdk_sample_key;
    CC = R0 == -1;
    IF CC JUMP .Lkey_stock;
    HOOK_RESULT R0
    HOOK_RESTORE
    RTS;
.Lkey_stock:
    HOOK_RESTORE
    LINK 0;
    JUMP.L stock_sample_key_tail;
.size extensions_sample_key, .-extensions_sample_key
