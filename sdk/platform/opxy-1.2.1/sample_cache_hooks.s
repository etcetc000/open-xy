/* Sample-analysis hooks that replace a load inside a stock function, so they
 * preserve every other register. The decisions are C++ in
 * sample_cache_stock.cpp. Neither hook is on the audio callback path.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_sample_cache_old_sum
.extern sdk_sample_cache_message_priority

/* Gather completion replaces the old path-sum load (R0). R6 is the new sum. */
.global sdk_sample_cache_snapshot
.type sdk_sample_cache_snapshot, STT_FUNC
sdk_sample_cache_snapshot:
    HOOK_SAVE
    R0 = R6;
    CALL sdk_sample_cache_old_sum;
    HOOK_RESULT R0
    HOOK_RESTORE
    RTS;
.size sdk_sample_cache_snapshot, .-sdk_sample_cache_snapshot

/* Replaces R3 = B[P5 + 100] (Z), the priority byte of the cache message P5
 * builds; stock stores R3 next. */
.global sdk_sample_cache_ipc_priority
.type sdk_sample_cache_ipc_priority, STT_FUNC
sdk_sample_cache_ipc_priority:
    HOOK_SAVE
    R0 = P5;
    CALL sdk_sample_cache_message_priority;
    HOOK_RESULT_R3 R0
    HOOK_RESTORE
    RTS;
.size sdk_sample_cache_ipc_priority, .-sdk_sample_cache_ipc_priority
