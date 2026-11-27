/* BF70x Programming Reference, CacheCtrl and Sync (8-155..8-158):
 * https://www.analog.com/media/en/dsp-documentation/processor-manuals/ADSP-BF70x_Blackfin_Programming_Reference.pdf
 * R0/R1 describe an aligned, nonempty resident-approved range. No cache mode,
 * MMR, interrupt mask, stack or callee-saved register is changed. A true return
 * means the range operations completed; hardware qualification is separate.
 */
.section .text
.align 2
.global sdk_collection_sync_code
.type sdk_collection_sync_code, STT_FUNC
sdk_collection_sync_code:
    R2 = R0 | R1;
    R3 = 31;
    R2 = R2 & R3;
    CC = R2 == 0;
    IF !CC JUMP .Lcache_invalid;
    CC = R1 == 0;
    IF CC JUMP .Lcache_invalid;
    R3 = R0 + R1;
    CC = R3 < R0 (IU);
    IF CC JUMP .Lcache_invalid;
    P0 = R0;
    P1 = R0;
    R2 = R1;
    R2 >>= 5;
    SSYNC;
.Lcache_data:
    FLUSH [P0++];
    R2 += -1;
    CC = R2 == 0;
    IF !CC JUMP .Lcache_data;
    SSYNC;
    R1 >>= 5;
.Lcache_instruction:
    IFLUSH [P1++];
    R1 += -1;
    CC = R1 == 0;
    IF !CC JUMP .Lcache_instruction;
    SSYNC;
    CSYNC;
    R0 = 1;
    RTS;
.Lcache_invalid:
    R0 = 0;
    RTS;
.size sdk_collection_sync_code, .-sdk_collection_sync_code

/* Dedicated, isolated line for a qualified coordinator's serial self-test.
 * It must never overlap a hook, baseline function, literal or mutable state.
 */
.section .text.collection_cache_scratch,"ax"
.balign 32
.global sdk_collection_cache_scratch
.type sdk_collection_cache_scratch, STT_FUNC
sdk_collection_cache_scratch:
    R0 = 0x1357 (X);
    RTS;
    .space 32-(.-sdk_collection_cache_scratch)
.size sdk_collection_cache_scratch, .-sdk_collection_cache_scratch

/* Same-processor GUI task mutex for the preallocated SVG working buffer.
 * Only the short compare/store masks interrupts; parsing never runs masked.
 */
.section .text
.align 2
.global sdk_collection_try_lock
.type sdk_collection_try_lock, STT_FUNC
sdk_collection_try_lock:
    P0 = R0;
    CLI R2;
    R0 = [P0];
    CC = R0 == 0;
    IF !CC JUMP .Lcollection_locked;
    R0 = 1;
    [P0] = R0;
    STI R2;
    SSYNC;
    RTS;
.Lcollection_locked:
    STI R2;
    R0 = 0;
    RTS;
.size sdk_collection_try_lock, .-sdk_collection_try_lock
.global sdk_collection_unlock
.type sdk_collection_unlock, STT_FUNC
sdk_collection_unlock:
    P0 = R0;
    SSYNC;
    R1 = 0;
    [P0] = R1;
    SSYNC;
    RTS;
.size sdk_collection_unlock, .-sdk_collection_unlock

.global sdk_collection_fence
.type sdk_collection_fence, STT_FUNC
sdk_collection_fence:
    SSYNC;
    RTS;
.size sdk_collection_fence, .-sdk_collection_fence
