/* OS 1.2.0 guarded Drumline transport branches. The track and registration
 * retain the actual machine ID; only the local classification is aliased.
 * Every patch replaces whole decoded instructions on the authenticated image.
 */
.include "sdk/platform/opxy-1.2.0/hook.inc"
.section .text
.align 2
.extern sdk_drum_alias
.extern stock_drum_classify_tail

.macro DRUM_ALIAS_R0
    HOOK_SAVE
    CALL sdk_drum_alias;
    HOOK_RESULT R0
    HOOK_RESTORE
.endm

/* 8d5b5b68: entry of the predicate for drum (3) or Drumline (4). */
.global extensions_drum_classify
.type extensions_drum_classify, STT_FUNC
extensions_drum_classify:
    DRUM_ALIAS_R0
    CC = R0 < 3 (IU);
    LINK 0;
    JUMP.L stock_drum_classify_tail;
.size extensions_drum_classify, .-extensions_drum_classify

/* 8d4e3408: preset region import, R3 is the constant 4. */
.global extensions_drum_preset_import
.type extensions_drum_preset_import, STT_FUNC
extensions_drum_preset_import:
    R0 = [P1 + 0x128];
    DRUM_ALIAS_R0
    CC = R0 == R3;
    RTS;
.size extensions_drum_preset_import, .-extensions_drum_preset_import

/* 8d4e3720 and 8d4e7210: import neutral track offsets and export regions. */
.global extensions_drum_compare_r0
.type extensions_drum_compare_r0, STT_FUNC
extensions_drum_compare_r0:
    DRUM_ALIAS_R0
    R1 = 4;
    CC = R0 == R1;
    RTS;
.size extensions_drum_compare_r0, .-extensions_drum_compare_r0

/* 8d4e6808: include the region even with an empty sample filename. */
.global extensions_drum_export_empty
.type extensions_drum_export_empty, STT_FUNC
extensions_drum_export_empty:
    R0 = [P1 + 0x18];
    DRUM_ALIAS_R0
    R1 = 4;
    CC = R0 == R1;
    RTS;
.size extensions_drum_export_empty, .-extensions_drum_export_empty

/* 8d4e6930: model/params/random export, R1 is already the constant 4. */
.global extensions_drum_export_parameters
.type extensions_drum_export_parameters, STT_FUNC
extensions_drum_export_parameters:
    R0 = [P1 + 0x18];
    DRUM_ALIAS_R0
    CC = R0 == R1;
    RTS;
.size extensions_drum_export_parameters, .-extensions_drum_export_parameters

/* 8d4e6b68: Drumline's optional gain field, R2 is the constant 4. */
.global extensions_drum_export_gain
.type extensions_drum_export_gain, STT_FUNC
extensions_drum_export_gain:
    R0 = [P1 + 0x18];
    DRUM_ALIAS_R0
    CC = R0 == R2;
    RTS;
.size extensions_drum_export_gain, .-extensions_drum_export_gain

/* 8d587560: project load's track check, R1 is already the constant 4. Stock
 * resets a Drumline track's eight parameters to neutral offsets there; a kit
 * that missed it kept whatever load left, zero, so every value lost 64. */
.global extensions_drum_load_parameters
.type extensions_drum_load_parameters, STT_FUNC
extensions_drum_load_parameters:
    R0 = [P1 + 0x18];
    DRUM_ALIAS_R0
    CC = R0 == R1;
    RTS;
.size extensions_drum_load_parameters, .-extensions_drum_load_parameters

/* 8d593d84: note start's Drumline test (R2 = 4; CC = R0 == R2). Only that
 * branch finds the played pad's region and packs its index into the note;
 * otherwise every voice renders pad index 0. R0 keeps the real engine, which
 * the region router receives next. */
.global extensions_drum_note_region
.type extensions_drum_note_region, STT_FUNC
extensions_drum_note_region:
    HOOK_SAVE
    CALL sdk_drum_alias;
    [SP + 20] = R0;
    HOOK_RESTORE
    R2 += -4;
    CC = R2 == 0;
    R2 = 4;
    RTS;
.size extensions_drum_note_region, .-extensions_drum_note_region

/* 8d58792e: local engine classification in the 24-region key lookup. Save
 * the aliased value into the saved R3 while preserving the track pointer R0,
 * index R1, and CC from stock's preceding key==0 test. */
.global extensions_drum_region_lookup
.type extensions_drum_region_lookup, STT_FUNC
extensions_drum_region_lookup:
    R3 = [P1 + 0x18];
    HOOK_SAVE
    R0 = R3;
    CALL sdk_drum_alias;
    [SP + 24] = R0;
    HOOK_RESTORE
    B[P5] = R1;
    RTS;
.size extensions_drum_region_lookup, .-extensions_drum_region_lookup

/* 8c0b1fb6: the absolute eight-byte call also displaces IF !CC JUMP 0x30
 * at 8c0b1fbc. Fallthrough is the call's return address 8c0b1fbe; stock's
 * non-drum branch is 0x2e bytes after it. Preserve R0 while redirecting RETS.
 * R2 is transient: the editor replaces it with the event after the test. */
.global extensions_drum_editor_classify
.type extensions_drum_editor_classify, STT_FUNC
extensions_drum_editor_classify:
    R2 = [FP + 8];
    HOOK_SAVE
    R0 = R2;
    CALL sdk_drum_alias;
    [SP + 20] = R0;
    HOOK_RESTORE
    R1 = 4;
    CC = R2 == R1;
    IF CC JUMP .Ldrum_editor_return;
    [--SP] = ASTAT;
    [--SP] = R0;
    R0 = RETS;
    R0 += 0x2e;
    RETS = R0;
    R0 = [SP++];
    ASTAT = [SP++];
.Ldrum_editor_return:
    RTS;
.size extensions_drum_editor_classify, .-extensions_drum_editor_classify
