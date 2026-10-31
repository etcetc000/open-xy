/* Only two reviewed stock calls are redirected. The background wrapper runs
 * stock first, then preserves every caller-scratch register across SDK service.
 * The receive wrapper restores original arguments/registers before stock data
 * is tail-forwarded. SDK packet handling never calls the sender here.
 */
.section .text
.include "sdk/platform/opxy-1.2.1/hook.inc"
.extern stock_telemetry_tick
.extern stock_telemetry_receive
.extern sdk_telemetry_audio_service
.extern sdk_telemetry_receive
.align 2
.global sdk_telemetry_tick
.type sdk_telemetry_tick, STT_FUNC
sdk_telemetry_tick:
    LINK 0;
    SP += -12;
    CALL stock_telemetry_tick;
    SP += 12;
    HOOK_SAVE;
    CALL sdk_telemetry_audio_service;
    HOOK_RESTORE;
    UNLINK;
    RTS;
.size sdk_telemetry_tick, .-sdk_telemetry_tick

.align 2
.global sdk_telemetry_receive_hook
.type sdk_telemetry_receive_hook, STT_FUNC
sdk_telemetry_receive_hook:
    HOOK_SAVE;
    CALL sdk_telemetry_receive;
    CC = R0 == 0;
    IF CC JUMP .Lforward_stock;
    HOOK_RESTORE;
    RTS;
.Lforward_stock:
    HOOK_RESTORE;
    JUMP.L stock_telemetry_receive;
.size sdk_telemetry_receive_hook, .-sdk_telemetry_receive_hook
