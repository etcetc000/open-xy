/* Per-key sends (drum_send.hpp) in stock 1.2.1's mixer, the engine block
 * 8d510760. Per instrument track it calls the voice loop 8d51c708 (only from
 * 8d510bea), which renders each voice into 0x11b01548 and adds it into the
 * track's stereo sum at 0x11903600 (8d51c7b0..8d51c7d8); the engine then scales
 * that sum in place by the track's gain (engine volume, scene mute, group level;
 * 8d510e14..8d510e98) and from it into the main mix, FX I's bus
 * (8d511088..8d5110c8) and FX II's (8d5110ca..8d51110e). Each patch replaces
 * one whole instruction outside every hardware loop body and replays it;
 * stock's own loops do all the mixing.
 *
 * These are the two send buses' sources, the sends' alone; the mixer's other
 * four sites, which the sends share with Sample Studio, are mixer_hooks.s.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_drum_send_state

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
