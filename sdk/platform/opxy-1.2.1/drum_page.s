/* Drum-page hooks whose register contract C++ cannot express. The page logic
 * is C++ in sample_controls_stock.cpp and drum_display_stock.cpp.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_sample_press_engine_owned
.extern sdk_sample_drum_fade_word

/* 8c0b2730 replaces a CALL with R0 = track; the caller keeps the button index
 * minus six (4..7) in R5. Pass it as the second argument. */
.global sdk_sample_press_engine
.type sdk_sample_press_engine, STT_FUNC
sdk_sample_press_engine:
    R1 = R5;
    JUMP.L sdk_sample_press_engine_owned;
.size sdk_sample_press_engine, .-sdk_sample_press_engine

/* 8d5355ac replaces R0 = [P1 + 20]; R0 >>>= 8 in the drum painter (P1 = the
 * pad record). Every other register is preserved. */
.global sdk_sample_drum_fade
.type sdk_sample_drum_fade, STT_FUNC
sdk_sample_drum_fade:
    HOOK_SAVE
    R0 = P1;
    CALL sdk_sample_drum_fade_word;
    HOOK_RESULT R0
    HOOK_RESTORE
    RTS;
.size sdk_sample_drum_fade, .-sdk_sample_drum_fade
