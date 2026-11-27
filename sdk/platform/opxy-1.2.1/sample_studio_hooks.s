/* Sample Studio (docs/sample-studio.md): the stubs.
 *
 * Five sites are entry branches, as the sequence page's are: the build writes
 * an absolute jump over the first eight bytes of the sample library
 * controller's press, Shift press, release and turn (vtable 0x8400caac, whose
 * slots are each method's only reference), and of the shared browser screen's
 * frame (slot 3 of 0x84058644, likewise its only reference), straight to the
 * C++ in sample_studio_host.cpp, which returns to stock's caller. No branch lands
 * inside any of those eight bytes.
 * The trampolines below give the C++ stock's own method: each replays the eight
 * bytes exactly and jumps to the instruction after them, whose epilogue then
 * returns to the C++ caller. P0 is never an argument, so it is free at every
 * entry.
 */
.section .text
.align 2

/* 8c0a330a, press (+0xc): P1 = R0; LINK 0; [--SP] = (R7:6, P5:5)
 * (0832, 00e8 0000, f505). */
.global sdk_sample_studio_stock_press
.type sdk_sample_studio_stock_press, STT_FUNC
sdk_sample_studio_stock_press:
    P1 = R0;
    LINK 0;
    [--SP] = (R7:6, P5:5);
    P0.H = 0x8c0a; P0.L = 0x3312;
    JUMP (P0);
.size sdk_sample_studio_stock_press, .-sdk_sample_studio_stock_press

/* 8c0a2be4, Shift press (+0x10): LINK 0; [--SP] = (R7:6, P5:4); SP += -12
 * (00e8 0000, f405, a66f). */
.global sdk_sample_studio_stock_shift_press
.type sdk_sample_studio_stock_shift_press, STT_FUNC
sdk_sample_studio_stock_shift_press:
    LINK 0;
    [--SP] = (R7:6, P5:4);
    SP += -12;
    P0.H = 0x8c0a; P0.L = 0x2bec;
    JUMP (P0);
.size sdk_sample_studio_stock_shift_press, .-sdk_sample_studio_stock_shift_press

/* 8c0a2d68, release (+0x14): LINK 0x1c; R0 = 0x37; [--SP] = (R7:6)
 * (00e8 0700, b861, 7005). */
.global sdk_sample_studio_stock_release
.type sdk_sample_studio_stock_release, STT_FUNC
sdk_sample_studio_stock_release:
    LINK 0x1c;
    R0 = 0x37 (X);
    [--SP] = (R7:6);
    P0.H = 0x8c0a; P0.L = 0x2d70;
    JUMP (P0);
.size sdk_sample_studio_stock_release, .-sdk_sample_studio_stock_release

/* 8c0a24d0, turn (+0x18): LINK 0; [--SP] = (R7:4); R0 += 0x10
 * (00e8 0000, 6005, 8064). */
.global sdk_sample_studio_stock_turn
.type sdk_sample_studio_stock_turn, STT_FUNC
sdk_sample_studio_stock_turn:
    LINK 0;
    [--SP] = (R7:4);
    R0 += 0x10;
    P0.H = 0x8c0a; P0.L = 0x24d8;
    JUMP (P0);
.size sdk_sample_studio_stock_turn, .-sdk_sample_studio_stock_turn

/* 8d55341c, the browser screen's frame (slot 3): LINK 0x38;
 * [--SP] = (R7:4, P5:3); SP += -0x14 (00e8 0e00, e305, 666f). */
.global sdk_sample_studio_stock_frame
.type sdk_sample_studio_stock_frame, STT_FUNC
sdk_sample_studio_stock_frame:
    LINK 0x38;
    [--SP] = (R7:4, P5:3);
    SP += -0x14;
    P0.H = 0x8d55; P0.L = 0x3424;
    JUMP (P0);
.size sdk_sample_studio_stock_frame, .-sdk_sample_studio_stock_frame
