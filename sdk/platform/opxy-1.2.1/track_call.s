/* The track call: stock 1.2.1's mixer (8d510760) calls each instrument track's
 * voice loop 8d51c708 from one place, 8d510bea, with R0 the voice manager, R1
 * the track and R2 its sum 0x11903600, which it has just cleared; the loop
 * returns in R0 the number of voices it rendered, which the engine's silence
 * check reads. Services that need this call share this one handler, in a fixed
 * order: the mixer's block begins (mixer.cpp: Sample Studio's player and the
 * per-key sends' reset), stock's loop runs as it always does (the mixer's
 * voice hook inside it included), then the track stage (track_stage.cpp)
 * processes the sum and gives the count the engine continues with. Linked
 * only with a track stage: a build with the mixer alone keeps
 * sdk_drum_send_track (mixer_hooks.s); without the mixer, sdk_mixer_begin
 * resolves to the SDK's no-op.
 *
 * The loop reads only R0-R2. Like any C function, it stores R2, P1 and P2 into
 * its caller's 12-byte argument area and reads them back only from there
 * (8d51c722..8d51c756, 8d51c7ea): this handler's frame provides that area.
 */
.section .text
.align 2
.extern sdk_mixer_begin
.extern sdk_track_stage
.global sdk_track_call
.type sdk_track_call, STT_FUNC
sdk_track_call:
    LINK 0;
    [--SP] = (R7:5);
    SP += -12;
    R5 = R0;
    R6 = R1;
    R7 = R2;
    CALL sdk_mixer_begin;
    R0 = R5;
    R1 = R6;
    R2 = R7;
    P0.H = 0x8d51; P0.L = 0xc708;
    CALL (P0);
    R2 = R0;
    R0 = R6;
    R1 = R7;
    CALL sdk_track_stage;
    SP += 12;
    (R7:5) = [SP++];
    UNLINK;
    RTS;
.size sdk_track_call, .-sdk_track_call
