/* Entry trampolines of the instrument controller's input methods
 * (page_layer_stock.cpp). Each replays exactly the eight bytes redirected in
 * stock 1.2.1 and jumps to the instruction after them; stock's epilogue then
 * returns to the C++ caller. P0 is caller-saved and not live at these entries.
 */
.section .text
.align 2

/* 8c0b2e4c: LINK 0; [--SP] = (R7:4, P5:3); SP += -0x14. */
.global sdk_page_stock_buttons
.type sdk_page_stock_buttons, STT_FUNC
sdk_page_stock_buttons:
    LINK 0;
    [--SP] = (R7:4, P5:3);
    SP += -0x14;
    P0.H = 0x8c0b; P0.L = 0x2e54;
    JUMP (P0);
.size sdk_page_stock_buttons, .-sdk_page_stock_buttons

/* 8c0b28a8: LINK 0; [--SP] = (R7:4, P5:3); P3 = R0. */
.global sdk_page_stock_shift_buttons
.type sdk_page_stock_shift_buttons, STT_FUNC
sdk_page_stock_shift_buttons:
    LINK 0;
    [--SP] = (R7:4, P5:3);
    P3 = R0;
    P0.H = 0x8c0b; P0.L = 0x28b0;
    JUMP (P0);
.size sdk_page_stock_shift_buttons, .-sdk_page_stock_shift_buttons

/* 8c0b12e4: LINK 0x44; [--SP] = (R7:4, P5:3); SP += -0x14. */
.global sdk_page_stock_turn
.type sdk_page_stock_turn, STT_FUNC
sdk_page_stock_turn:
    LINK 0x44;
    [--SP] = (R7:4, P5:3);
    SP += -0x14;
    P0.H = 0x8c0b; P0.L = 0x12ec;
    JUMP (P0);
.size sdk_page_stock_turn, .-sdk_page_stock_turn
