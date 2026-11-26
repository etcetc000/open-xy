/* The device identity's GUI service (device_identity.cpp). Stock 1.2.1's main
 * loop (8c0b72ac) calls 8d57c0f8, which takes no arguments, on every pass from
 * 8c0b73a8, whatever the screen. This handler makes that call as stock did,
 * then runs the service with every register as the call left it, and returns
 * them so: the loop continues exactly as after stock's own call.
 */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_identity_gui_service
.global sdk_identity_gui_tick
.type sdk_identity_gui_tick, STT_FUNC
sdk_identity_gui_tick:
    LINK 0;
    SP += -12;
    P0.H = 0x8d57; P0.L = 0xc0f8;
    CALL (P0);
    SP += 12;
    HOOK_SAVE
    CALL sdk_identity_gui_service;
    HOOK_RESTORE
    UNLINK;
    RTS;
.size sdk_identity_gui_tick, .-sdk_identity_gui_tick
