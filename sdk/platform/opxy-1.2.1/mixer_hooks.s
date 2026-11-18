/* Stock 1.2.1's mixer, the engine block 8d510760: four sites the per-key sends
 * (drum_send.hpp, service drum_sends) and Sample Studio's player (service
 * sample_studio) share, as service mixer. Per instrument track the engine calls
 * the voice loop 8d51c708 (only from 8d510bea), which renders each voice into
 * 0x11b01548 and adds it into the track's stereo sum at 0x11903600
 * (8d51c7b0..8d51c7d8); the engine then scales that sum in place by the track's
 * gain (8d510e14..8d510e98) and from it into the main mix and the two send
 * buses. Each patch replaces one whole instruction outside every hardware loop
 * body and replays it; the C++ side (mixer.cpp) hands each site to both users.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2

.extern sdk_mixer_begin
.extern sdk_mixer_voice
.extern sdk_mixer_gain
.extern sdk_mixer_own

/* 8d510bea: CALL 0x8d51c708, the track's voice loop. Its arguments (R0-R2 and
 * the stack) and return are untouched: after the block begins, this jumps to
 * the loop with the engine's own return address, which the loop returns to. */
.global sdk_drum_send_track
.type sdk_drum_send_track, STT_FUNC
sdk_drum_send_track:
    HOOK_SAVE
    CALL sdk_mixer_begin;
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
    CALL sdk_mixer_voice;
    HOOK_RESTORE
    P2.H = 0x11b0; P2.L = 0x1548;
    RTS;
.size sdk_drum_send_voice_hook, .-sdk_drum_send_voice_hook

/* 8d510ea8: P2 = 0x11903600, just after stock scaled the track's sum in place
 * by the track's gain (8d510e60..8d510e98, a hardware loop ending at 8d510e8e).
 * R0 holds the gain the ramp reached and R2 its step per frame (8d510eb0 stores
 * the gain); the engine keeps the track index at [FP + 8]. Gives the sums the
 * track kept the same ramp, then replays the load. HOOK_SAVE leaves every
 * register as it found it. */
.global sdk_drum_send_gain_hook
.type sdk_drum_send_gain_hook, STT_FUNC
sdk_drum_send_gain_hook:
    HOOK_SAVE
    R1 = R0;
    P0 = [FP];
    R0 = [P0 + 8];
    CALL sdk_mixer_gain;
    HOOK_RESTORE
    P2.H = 0x1190; P2.L = 0x3600;
    RTS;
.size sdk_drum_send_gain_hook, .-sdk_drum_send_gain_hook

/* 8d511116: P0 = [SP + 0x48], just after FX II's loop has stored its last word
 * (8d51110e). Adds the track's own-amount sums into the FX buses at the
 * engine's level for the track ([FP - 0x68], written once at 8d510d38, the
 * gain stock gives the track's dry mix and sends alike), and the voices with
 * their own amounts into the engine's main out at the dry those amounts give,
 * before stock's dry loop (8d511122..8d51117e) adds the whole sum at the
 * track's; then replays the load. HOOK_SAVE's LINK keeps the engine's frame
 * pointer at [FP], which the C++ side also gets (R2) to read the engine's
 * FX I gain, dry smoother and main out. */
.global sdk_drum_send_own
.type sdk_drum_send_own, STT_FUNC
sdk_drum_send_own:
    HOOK_SAVE
    P0 = [FP];
    R0 = [P0 + 8];
    R1 = [P0 + -0x68];
    R2 = P0;
    CALL sdk_mixer_own;
    HOOK_RESTORE
    P0 = [SP + 0x48];
    RTS;
.size sdk_drum_send_own, .-sdk_drum_send_own
