/* 8d50fff4: stock's bundle stores frame 63's right sample and reads a mix flag.
 * Commit that sample before applying the envelope. Read the flag on return. */
.section .text
.align 2
.extern sdk_input_monitor_audio
.global sdk_input_monitor_audio_hook
.type sdk_input_monitor_audio_hook, STT_FUNC
sdk_input_monitor_audio_hook:
    [P1] = R3;
    LINK 0;
    [--SP] = ASTAT;
    [--SP] = R1;
    [--SP] = R2;
    [--SP] = R3;
    [--SP] = P0;
    [--SP] = P1;
    [--SP] = P2;
    SP += -12;
    CALL sdk_input_monitor_audio;
    SP += 12;
    P2 = [SP++];
    P1 = [SP++];
    P0 = [SP++];
    R3 = [SP++];
    R2 = [SP++];
    R1 = [SP++];
    ASTAT = [SP++];
    UNLINK;
    R0 = B[P0] (Z);
    RTS;
.size sdk_input_monitor_audio_hook, .-sdk_input_monitor_audio_hook
