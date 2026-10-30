/* 11a04260: engine-group modulation CALL.A 8d518ec4. The original C
 * arguments are project, track, slot and amount. R4 still holds the full
 * selector >> 8. Pass it as a fifth argument; all callee-saved registers
 * retain their stock values. Other destination groups keep their own calls.
 */
.section .text
.include "sdk/platform/opxy-1.2.1/hook.inc"
.align 2
.extern sdk_lfo_modulate
.global sdk_lfo_audio
.type sdk_lfo_audio, STT_FUNC
sdk_lfo_audio:
    LINK 0;
    SP += -20;
    R3 = [FP + 20];
    [SP + 12] = R3;
    [SP + 16] = R4;
    CALL sdk_lfo_modulate;
    SP += 20;
    UNLINK;
    RTS;
.size sdk_lfo_audio, .-sdk_lfo_audio

/* Each GUI caller already decoded its group and raw destination into frame
 * locals. Preserve the animation's four C arguments and pass those locals
 * as arguments five and six. LINK saves the caller's FP at [FP].
 */
.extern sdk_lfo_icon_slide
.global sdk_lfo_element_icon
.type sdk_lfo_element_icon, STT_FUNC
sdk_lfo_element_icon:
    LINK 0;
    SP += -24;
    R3 = [FP + 20];
    [SP + 12] = R3;
    P0 = [FP];
    R3 = [P0 - 0x34];
    [SP + 16] = R3;
    R3 = [P0 - 0x38];
    [SP + 20] = R3;
    CALL sdk_lfo_icon_slide;
    SP += 24;
    UNLINK;
    RTS;
.size sdk_lfo_element_icon, .-sdk_lfo_element_icon

.global sdk_lfo_random_icon
.type sdk_lfo_random_icon, STT_FUNC
sdk_lfo_random_icon:
    LINK 0;
    SP += -24;
    R3 = [FP + 20];
    [SP + 12] = R3;
    P0 = [FP];
    R3 = [P0 - 0x28];
    [SP + 16] = R3;
    R3 = [P0 - 0x30];
    [SP + 20] = R3;
    CALL sdk_lfo_icon_slide;
    SP += 24;
    UNLINK;
    RTS;
.size sdk_lfo_random_icon, .-sdk_lfo_random_icon

.global sdk_lfo_value_icon
.type sdk_lfo_value_icon, STT_FUNC
sdk_lfo_value_icon:
    LINK 0;
    SP += -24;
    R3 = [FP + 20];
    [SP + 12] = R3;
    P0 = [FP];
    R3 = [P0 - 0x38];
    [SP + 16] = R3;
    R3 = [P0 - 0x30];
    [SP + 20] = R3;
    CALL sdk_lfo_icon_slide;
    SP += 24;
    UNLINK;
    RTS;
.size sdk_lfo_value_icon, .-sdk_lfo_value_icon

/* 8d53ffb0: group, raw slot and flag are the stock name arguments. P3
 * holds the Element page; its +0x58 word is the knob-strip widget.
 */
.extern sdk_lfo_element_label
.global sdk_lfo_element_name
.type sdk_lfo_element_name, STT_FUNC
sdk_lfo_element_name:
    LINK 0;
    SP += -16;
    R3 = [P3 + 0x58];
    [SP + 12] = R3;
    CALL sdk_lfo_element_label;
    SP += 16;
    UNLINK;
    RTS;
.size sdk_lfo_element_name, .-sdk_lfo_element_name

/* 8d53f888: Element initializes its strip before the first animation. The
 * preceding instruction already saved the full slot at [P5 + 0x18]. R7 is
 * sdk_lfo_range's ownership/group result. Map only the icon offset, then
 * replay the displaced fixed-point multiply; R1 and R0 remain its outputs.
 */
.global sdk_lfo_element_initial_icon
.extern sdk_lfo_icon_slot
.type sdk_lfo_element_initial_icon, STT_FUNC
sdk_lfo_element_initial_icon:
    HOOK_SAVE
    R1 = R7;
    CALL sdk_lfo_icon_slot;
    HOOK_RESULT R0
    HOOK_RESTORE
    R0 <<= 10;
    (R1:0) = R1 * R0 (IS, NS);
    R1 <<= 22;
    RTS;
.size sdk_lfo_element_initial_icon, .-sdk_lfo_element_initial_icon
