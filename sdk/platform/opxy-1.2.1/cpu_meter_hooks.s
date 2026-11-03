/* The cpu meter's hooks (cpu_meter.cpp). */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2

/* DSP board: 8c0c6ffe, the audio task's CALL.A 11a02864 (R0 the TX buffer, R1
 * TX + 0x400, R2), the whole DSP callback for one 64-sample block: input copy,
 * every track, the mixer, sends and master. Reads CYCLES either side and passes
 * both to C++. A call replaced by a call: the arguments reach stock untouched,
 * its result comes back in R0, and P3, which the task reads after it, is
 * callee-saved. */
.extern sdk_cpu_block
.global sdk_cpu_audio
.type sdk_cpu_audio, STT_FUNC
sdk_cpu_audio:
    LINK 0;
    [--SP] = (R7:6);
    SP += -12;
    R6 = CYCLES;
    P1.H = 0x11a0; P1.L = 0x2864;
    CALL (P1);
    R7 = R0;
    R1 = CYCLES;
    R0 = R6;
    CALL sdk_cpu_block;
    R0 = R7;
    SP += 12;
    (R7:6) = [SP++];
    UNLINK;
    RTS;
.size sdk_cpu_audio, .-sdk_cpu_audio

/* DSP board: a mixer call replaced by a timed one. Calls the stock function at
 * `high`:`low`, or `symbol` when given, with the caller's arguments (the callees read only R0-R2 and the 12-byte argument area
 * this frame provides) and passes its cycles to sdk_cpu_track_time as `part`,
 * or the track in R1 when `part` is R1; the callee's result comes back in R0. */
.extern sdk_cpu_track_time
.macro TIMED_CALL name, high, low, part, symbol
.global \name
.type \name, STT_FUNC
\name:
    LINK 4;
    [--SP] = (R7:6);
    SP += -12;
    R6 = CYCLES;
    R7 = \part;
.ifb \symbol
    P1.H = \high; P1.L = \low;
    CALL (P1);
.else
    CALL \symbol;
.endif
    [FP - 4] = R0;
    R1 = CYCLES;
    R1 = R1 - R6;
    R0 = R7;
    CALL sdk_cpu_track_time;
    R0 = [FP - 4];
    SP += 12;
    (R7:6) = [SP++];
    UNLINK;
    RTS;
.size \name, .-\name
.endm

/* Parts as cpu_meter.hpp numbers them: tracks 0-7, then the stages. */
/* 8d510bea: CALL 8d51c708, one track's synth voices (R0 the voice pool, R1 the track). */
TIMED_CALL sdk_cpu_track, 0x8d51, 0xc708, R1
/* 8d51234e: CALL 8d50fdc0, aux 5's processor. */
TIMED_CALL sdk_cpu_aux, 0x8d50, 0xfdc0, 8
/* 8d511486: CALL 8d50fa88, tape. */
TIMED_CALL sdk_cpu_tape, 0x8d50, 0xfa88, 9
/* 8d5116a0 and 8d511908: CALL 8d50f594, FX I and FX II. */
TIMED_CALL sdk_cpu_fx1, 0x8d50, 0xf594, 10
TIMED_CALL sdk_cpu_fx2, 0x8d50, 0xf594, 11
/* 8d511b80: CALL.A 11a02c10, the master stage (8 bytes with its NOP), through
 * sdk_master_stage (startup.cpp): the selected master processor when the build
 * links the master processor service, otherwise stock's EQ. */
.extern sdk_master_stage
TIMED_CALL sdk_cpu_master, , , 12, sdk_master_stage

/* Stock's master stage, the EQ: sdk_master_stage's target when the build has no
 * master processor, and a browser selection's fallback for sdk_master_process
 * (tooling.browser_kit). Stock returns to the caller; P1 is caller-saved. */
.global sdk_master_stock_stage
.type sdk_master_stock_stage, STT_FUNC
sdk_master_stock_stage:
    P1.H = 0x11a0; P1.L = 0x2c10;
    JUMP (P1);
.size sdk_master_stock_stage, .-sdk_master_stock_stage

/* DSP board: 8c0c2384, inside the bridge dispatcher 8c0c236c that runs the main
 * board's requests (P5 the request, R1 9), in place of R0 = B[P5 + 8] (X);
 * CC = R0 < R1 (IU); IF CC JUMP 8c0c23f6. C++ answers the meter's poll and gives
 * the command to dispatch: the request's own, or 0x31 for the meter's, which
 * stock completes without acting, so the reply carries the answer back. P1 is
 * dead here: every path writes it before reading it. */
.extern sdk_cpu_request
.global sdk_cpu_dispatch
.type sdk_cpu_dispatch, STT_FUNC
sdk_cpu_dispatch:
    HOOK_SAVE
    R0 = P5;
    CALL sdk_cpu_request;
    HOOK_RESULT R0
    HOOK_RESTORE
    CC = R0 < R1 (IU);
    IF !CC JUMP 1f;
    P1.H = 0x8c0c; P1.L = 0x23f6;
    RETS = P1;
1:
    RTS;
.size sdk_cpu_dispatch, .-sdk_cpu_dispatch

/* Main board: Project > M4 > cpu (cpu_meter.cpp). Each hook below acts only
 * when the renderer's this (P4) is that page; on any other category it runs
 * stock's instructions unchanged. */
.extern sdk_cpu_page_object
.extern sdk_cpu_page_add
.extern sdk_cpu_page_format
.extern sdk_cpu_page_key
.extern sdk_cpu_page_rows
.extern sdk_cpu_page_tail

/* 8c088456, the category list builder (R7 the list, R1 midi), in place of
 * R0 = R7; CALL 8c09b830; SP += 0xc. Adds midi, then the cpu page; stock calls
 * surround the site, so no caller-saved register is live. */
.global sdk_cpu_page_list
.type sdk_cpu_page_list, STT_FUNC
sdk_cpu_page_list:
    [--SP] = RETS;
    SP += -12;
    R0 = R7;
    P1.H = 0x8c09; P1.L = 0xb830;
    CALL (P1);
    R0 = R7;
    CALL sdk_cpu_page_add;
    SP += 12;
    RETS = [SP++];
    SP += 0xc;
    RTS;
.size sdk_cpu_page_list, .-sdk_cpu_page_list

/* 8c09a4fa, in place of R2 = 0x8400c204 ("%d/24", the header value's format). */
.global sdk_cpu_page_fmt
.type sdk_cpu_page_fmt, STT_FUNC
sdk_cpu_page_fmt:
    R2.H = 0x8400; R2.L = 0xc204;
    P1.L = sdk_cpu_page_object; P1.H = sdk_cpu_page_object;
    P1 = [P1];
    CC = P4 == P1;
    IF !CC JUMP 1f;
    HOOK_SAVE
    CALL sdk_cpu_page_format;
    [SP + 20] = R0;     /* the saved R2 */
    HOOK_RESTORE
1:
    RTS;
.size sdk_cpu_page_fmt, .-sdk_cpu_page_fmt

/* 8c09a50c, in place of R1 = 0x8400c204, which R1 += 8 then makes "assigned":
 * on the cpu page, its key 8 bytes before. */
.global sdk_cpu_page_hdr
.type sdk_cpu_page_hdr, STT_FUNC
sdk_cpu_page_hdr:
    R1.H = 0x8400; R1.L = 0xc204;
    P1.L = sdk_cpu_page_object; P1.H = sdk_cpu_page_object;
    P1 = [P1];
    CC = P4 == P1;
    IF !CC JUMP 1f;
    R1.L = sdk_cpu_page_key; R1.H = sdk_cpu_page_key;
    R1 += -8;
1:
    RTS;
.size sdk_cpu_page_hdr, .-sdk_cpu_page_hdr

/* 8c09a544, in place of R0 = R5; CALL 8c086d28 (the header row freed);
 * R0 = [SP + 0x9c]; R1 = 0x10. On the cpu page its settings go in first; the
 * stock call here leaves no other caller-saved register live. */
.global sdk_cpu_page_more
.type sdk_cpu_page_more, STT_FUNC
sdk_cpu_page_more:
    [--SP] = RETS;
    SP += -12;
    P1.L = sdk_cpu_page_object; P1.H = sdk_cpu_page_object;
    P1 = [P1];
    CC = P4 == P1;
    IF !CC JUMP 1f;
    R0 = R5;
    CALL sdk_cpu_page_rows;
1:
    R0 = R5;
    P1.H = 0x8c08; P1.L = 0x6d28;
    CALL (P1);
    SP += 12;
    RETS = [SP++];
    R0 = [SP + 0x9c];
    R1 = 0x10 (X);
    RTS;
.size sdk_cpu_page_more, .-sdk_cpu_page_more

/* 8c09a638, after the track rows, in place of R1 = [SP + 0xa8]; R2 = [SP + 0xa4]
 * (the row vector's end and begin; the vector is at the caller's SP + 0xa0). On
 * the cpu page the track rows are dropped first, so the count leaves them out. */
.global sdk_cpu_page_end
.type sdk_cpu_page_end, STT_FUNC
sdk_cpu_page_end:
    P1.L = sdk_cpu_page_object; P1.H = sdk_cpu_page_object;
    P1 = [P1];
    CC = P4 == P1;
    IF !CC JUMP 1f;
    HOOK_SAVE
    R0 = FP;
    R1 = 0xa8 (X);      /* above the saved FP and RETS */
    R0 = R0 + R1;
    CALL sdk_cpu_page_tail;
    HOOK_RESTORE
1:
    R1 = [SP + 0xa8];
    R2 = [SP + 0xa4];
    RTS;
.size sdk_cpu_page_end, .-sdk_cpu_page_end
