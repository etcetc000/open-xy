/* Stock sample dispatch hooks whose register contract C++ cannot express. The
 * decisions are C++ (sample_machines.cpp, drum_controls_stock.cpp); these
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

.extern stock_sample_slice_receive_tail
.extern sdk_sample_slice_receive

/* Audio message receiver entry. R0 is its controller, R1 is a complete stock
 * framed packet. Read the optional slicer extension before stock consumes the
 * ordinary note prefix; replay the displaced LINK after restoring all state. */
.global extensions_sample_slice_receive
.type extensions_sample_slice_receive, STT_FUNC
extensions_sample_slice_receive:
    HOOK_SAVE
    CALL sdk_sample_slice_receive;
    HOOK_RESTORE
    LINK 28;
    JUMP.L stock_sample_slice_receive_tail;
.size extensions_sample_slice_receive, .-extensions_sample_slice_receive

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

/* 8d4e6a7e: the preset writer has a separate engine-ID switch before its
 * 24-region serializer. Alias only that switch; the track's real ID has
 * already supplied the saved type and remains intact for region metadata.
 * Replaces R0 = [P1 + 24]; CC = R0 == 3. */
.global sdk_sample_preset_regions
.type sdk_sample_preset_regions, STT_FUNC
sdk_sample_preset_regions:
    HOOK_SAVE
    R0 = [P1 + 24];
    CALL sdk_sample_alias;
    HOOK_RESULT R0
    HOOK_RESTORE
    CC = R0 == 3;
    RTS;
.size sdk_sample_preset_regions, .-sdk_sample_preset_regions

/* Replaces R5 = 30; CC = R7 == R5 in stock's note-region dispatcher (R7 =
 * engine; stock's frame restores R7 on return). sdk_sample_note_route decides:
 * kStockRoute (0) runs stock's test. kOwnedSampler (3) selects the sampler's
 * payload (R7 = 2), which stock's test then routes as the sampler (CC = 0).
 * Otherwise the owned engine selects the multisampler payload (R7 = 30); the
 * track and factory keep the real engine either way. kOwnedNearest (1) keeps
 * the nearest-region test; kOwnedDrumPads (2) skips it (CC = 0). */
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
    CC = R5 == 3;
    IF CC JUMP .Lnote_sampler;
    R7 = 30;
    CC = R5 == 1;
    IF CC JUMP .Lnote_nearest;
    R5 = 30;
    CC = R5 == 0;
    RTS;
.Lnote_sampler:
    R7 = 2;
.Lnote_nearest:
    R5 = 30;
    CC = R7 == R5;
    RTS;
.size extensions_sample_note_region, .-extensions_sample_note_region

/* Entry of stock's key-to-region lookup (R0 = track, R1 = key). sdk_sample_key
 * returns region 0 for an owned page-2 engine, the canonical pad of an owned
 * drum page, or -1 to continue stock with every register as it arrived. */
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
