/* The System descriptor-vector copy at 8c098f32 is more than the Blackfin
 * relative-CALL range from the module. Its 12-byte replacement is therefore
 * an absolute call. The span includes the following stock P1 load; restore
 * every live stock register around C++, then replay that load before returning
 * to the next complete stock instruction. */
.include "sdk/platform/opxy-1.2.1/hook.inc"
.section .text
.align 2
.extern sdk_extensions_system_entries

.global sdk_extensions_system_entries_hook
.type sdk_extensions_system_entries_hook, STT_FUNC
sdk_extensions_system_entries_hook:
    HOOK_SAVE
    CALL sdk_extensions_system_entries
    HOOK_RESTORE
    P1.H = 0x8400;
    P1.L = 0xc0ec;
    RTS;
.size sdk_extensions_system_entries_hook, .-sdk_extensions_system_entries_hook
