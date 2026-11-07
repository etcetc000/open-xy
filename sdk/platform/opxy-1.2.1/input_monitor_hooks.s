/* 8d50ffec: input scratch has been rendered; all music and FX additions follow.
 * Replay the complete P0 load after preserving live caller-saved registers.
 * No stock branch targets the eight-byte load's interior. */
.section .text
.align 2
.extern sdk_input_monitor_audio
.global sdk_input_monitor_audio_hook
.type sdk_input_monitor_audio_hook, STT_FUNC
sdk_input_monitor_audio_hook:
    LINK 0;
    [--SP] = ASTAT;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = R2;
    [--SP] = R3;
    [--SP] = P1;
    [--SP] = P2;
    SP += -12;
    CALL sdk_input_monitor_audio;
    SP += 12;
    P2 = [SP++];
    P1 = [SP++];
    R3 = [SP++];
    R2 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    ASTAT = [SP++];
    UNLINK;
    P0.H = 0x11b0;
    P0.L = 0x1dd4;
    RTS;
.size sdk_input_monitor_audio_hook, .-sdk_input_monitor_audio_hook
