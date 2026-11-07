/* Per-key sends (drum_send.hpp) in stock 1.2.1's mixer, the engine block
 * 8d510760. Per instrument track it calls the voice loop 8d51c708 (only from
 * 8d510bea), which renders each voice into 0x11b01548 and adds it into the
 * track's stereo sum at 0x11903600 (8d51c7b0..8d51c7d8); the engine then scales
 * that sum into the main mix, FX I's bus (8d511088..8d5110c8) and FX II's
 * (8d5110ca..8d51110e). Each patch replaces one whole instruction outside every
 * hardware loop body and replays it; stock's own loops do all the mixing.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_drum_send_begin
.extern sdk_drum_send_voice
.extern sdk_drum_send_state
.extern sdk_drum_send_own_mix

/* 8d510bea: CALL 0x8d51c708, the track's voice loop. Its arguments (R0-R2 and
 * the stack) and return are untouched: after the state is reset, this jumps to
 * the loop with the engine's own return address, which the loop returns to. */
.global sdk_drum_send_track
.type sdk_drum_send_track, STT_FUNC
sdk_drum_send_track:
    HOOK_SAVE
    CALL sdk_drum_send_begin;
    HOOK_RESTORE
    P0.H = 0x8d51; P0.L = 0xc708;
    JUMP (P0);
.size sdk_drum_send_track, .-sdk_drum_send_track

/* 8d51c7b2: P2 = 0x11b01548, the rendered voice the loop is about to add. The
 * loop holds the voice in R4 (8d51c77e) and the track in R6 (8d51c71e). */
.global sdk_drum_send_voice_hook
.type sdk_drum_send_voice_hook, STT_FUNC
sdk_drum_send_voice_hook:
    HOOK_SAVE
    R0 = R4;
    R1 = R6;
    CALL sdk_drum_send_voice;
    HOOK_RESTORE
    P2.H = 0x11b0; P2.L = 0x1548;
    RTS;
.size sdk_drum_send_voice_hook, .-sdk_drum_send_voice_hook

/* 8d511088 (FX I) and 8d5110ca (FX II): P1 = 0x11903600, the sum each send's
 * loop reads. The engine keeps the track index at [FP + 8] (8d510a40). A track
 * whose voices kept their own shares of that send reads the kept sum; every
 * other the same address as stock. Only P1 changes. Each reads its send's
 * DrumSendBus in sdk_drum_send_state: the track plus one, then the source. */
.global sdk_drum_send_source_1
.type sdk_drum_send_source_1, STT_FUNC
sdk_drum_send_source_1:
    [--SP] = ASTAT;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = P0;
    P1.H = 0x1190; P1.L = 0x3600;
    P0.H = sdk_drum_send_state;
    P0.L = sdk_drum_send_state;
    R0 = [P0 + 0];
    R1 = [FP + 8];
    R1 += 1;
    CC = R0 == R1;
    R0 = [P0 + 4];
    IF CC P1 = R0;
    P0 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    ASTAT = [SP++];
    RTS;
.size sdk_drum_send_source_1, .-sdk_drum_send_source_1

.global sdk_drum_send_source_2
.type sdk_drum_send_source_2, STT_FUNC
sdk_drum_send_source_2:
    [--SP] = ASTAT;
    [--SP] = R0;
    [--SP] = R1;
    [--SP] = P0;
    P1.H = 0x1190; P1.L = 0x3600;
    P0.H = sdk_drum_send_state;
    P0.L = sdk_drum_send_state;
    R0 = [P0 + 8];
    R1 = [FP + 8];
    R1 += 1;
    CC = R0 == R1;
    R0 = [P0 + 12];
    IF CC P1 = R0;
    P0 = [SP++];
    R1 = [SP++];
    R0 = [SP++];
    ASTAT = [SP++];
    RTS;
.size sdk_drum_send_source_2, .-sdk_drum_send_source_2

/* 8d511116: P0 = [SP + 0x48], just after FX II's loop has stored its last word
 * (8d51110e). Adds the track's own-amount sums into the FX buses at the
 * engine's level for the track ([FP - 0x68], written once at 8d510d38, the
 * gain stock gives the track's dry mix and sends alike), then replays the
 * load. HOOK_SAVE's LINK keeps the engine's frame pointer at [FP]. */
.global sdk_drum_send_own
.type sdk_drum_send_own, STT_FUNC
sdk_drum_send_own:
    HOOK_SAVE
    P0 = [FP];
    R0 = [P0 + 8];
    R1 = [P0 + -0x68];
    CALL sdk_drum_send_own_mix;
    HOOK_RESTORE
    P0 = [SP + 0x48];
    RTS;
.size sdk_drum_send_own, .-sdk_drum_send_own
