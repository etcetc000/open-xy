/* Entry trampolines replay exactly the eight bytes redirected in stock 1.2.1.
 * Their C++ callers use ordinary ABI calls; stock epilogues return to them.
 * P0 is caller-saved and is not live at these entry continuations.
 */
.section .text
.align 2
.global sdk_master_stock_buttons
.type sdk_master_stock_buttons, STT_FUNC
sdk_master_stock_buttons:
    LINK 0;
    [--SP] = (R7:5, P5:3);
    P4 = R1;
    P0.H = 0x8c09; P0.L = 0x3a78;
    JUMP (P0);
.size sdk_master_stock_buttons, .-sdk_master_stock_buttons

.global sdk_master_stock_shift_buttons
.type sdk_master_stock_shift_buttons, STT_FUNC
sdk_master_stock_shift_buttons:
    P1 = R1;
    LINK 12;
    R1 = 12;
    P0.H = 0x8c09; P0.L = 0x3934;
    JUMP (P0);
.size sdk_master_stock_shift_buttons, .-sdk_master_stock_shift_buttons

.global sdk_master_stock_turn
.type sdk_master_stock_turn, STT_FUNC
sdk_master_stock_turn:
    R3 = 8;
    CC = R1 < R3 (IU);
    LINK 12;
    P0.H = 0x8c09; P0.L = 0x3890;
    JUMP (P0);
.size sdk_master_stock_turn, .-sdk_master_stock_turn

.global sdk_master_stock_frame
.type sdk_master_stock_frame, STT_FUNC
sdk_master_stock_frame:
    LINK 144;
    [--SP] = (R7:4, P5:3);
    SP += -20;
    P0.H = 0x8d53; P0.L = 0x7d0c;
    JUMP (P0);
.size sdk_master_stock_frame, .-sdk_master_stock_frame

.global sdk_project_stock_new
.type sdk_project_stock_new, STT_FUNC
sdk_project_stock_new:
    LINK 21552;
    [--SP] = (R7:4, P5:3);
    SP += -20;
    P0.H = 0x8d58; P0.L = 0x811c;
    JUMP (P0);
.size sdk_project_stock_new, .-sdk_project_stock_new
