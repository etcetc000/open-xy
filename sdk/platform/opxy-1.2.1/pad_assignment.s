/* Pad assignment hooks whose register contract C++ cannot express: two retry
 * hooks that replace literal loads inside stock functions, and the owned
 * region initializer call, whose return address the fade hook recognizes.
 * The decisions are C++ in pad_assignment_stock.cpp.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_sample_retry

/* Replaces a literal load whose register \reg the stub reloads with \high:\low.
 * R0 is a stock path-equality result; sdk_sample_retry clears it when an owned
 * pad may retry. Returns CC = R0 == 0, as stock tests next. */
.macro RETRY name, pending, reg, high, low
.global \name
.type \name, STT_FUNC
\name:
    HOOK_SAVE
    R1 = \pending;
    CALL sdk_sample_retry;
    HOOK_RESULT R0
    HOOK_RESTORE
    CC = R0 == 0;
    \reg\().H = \high;
    \reg\().L = \low;
    RTS;
.size \name, .-\name
.endm

/* 8c0a1bbc: active-file equality. Returning to the active file after queuing
 * another must update the pending request, or its stale path equality
 * discards A in A/B/A. */
RETRY sdk_sample_active_retry, 0, P4, 0x0801, 0xf424
/* 8c0a1bd6: pending-path equality. A stale header completion retries the
 * pending path, which stock's duplicate check drops even with the worker idle. */
RETRY sdk_sample_pending_retry, 1, P5, 0x0801, 0xf408

/* R0=track, R1=raw key, R2=settings, stack: frames, last, canonical key. Calls stock
 * 8d589b8c(track, key, 60, 0, frames, frames, last) with the settings at
 * FP-8 and the canonical key at FP-12, and the return address
 * sdk_sample_assign_owned_return, which is how the fade hook below recognizes
 * this call without shared state. */
.global sdk_sample_assign_owned
.type sdk_sample_assign_owned, STT_FUNC
sdk_sample_assign_owned:
    LINK 48;
    [SP + 40] = R2;
    R2 = [FP + 28];
    [SP + 36] = R2;
    R2 = [FP + 20];
    [SP + 16] = R2;
    [SP + 20] = R2;
    R2 = [FP + 24];
    [SP + 24] = R2;
    R2 = 0;
    [SP + 12] = R2;
    R2 = 60;
    P0 = 0x8d589b8c;
    CALL (P0);
.global sdk_sample_assign_owned_return
sdk_sample_assign_owned_return:
    UNLINK;
    RTS;
.size sdk_sample_assign_owned, .-sdk_sample_assign_owned

/* Only the fade conversion at 8d589c40 is redirected here. Stock initializer
 * LINK 0 leaves its caller's return PC at [FP+4]. Recognizing our owned call
 * site scopes this override without shared state or changing other callers.
 * Its explicit inner-marker args skip the earlier 20%/80% conversions. */
.global sdk_sample_assign_fade
.type sdk_sample_assign_fade, STT_FUNC
sdk_sample_assign_fade:
    R1 = [FP + 4];
    R2.H = sdk_sample_assign_owned_return;
    R2.L = sdk_sample_assign_owned_return;
    CC = R1 == R2;
    IF !CC JUMP .Lassign_fade_stock;
    P0 = [FP];
    /* Stock writes R4 to the region key after this call. */
    R4 = [P0 - 12];
    R0 = [P0 - 8];
    RTS;
.Lassign_fade_stock:
    LINK 12;
    P0 = 0x04020fd8;
    CALL (P0);
    UNLINK;
    RTS;
.size sdk_sample_assign_fade, .-sdk_sample_assign_fade
