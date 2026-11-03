/* The System descriptor-vector copy at 8c098f32 is more than the Blackfin
 * relative-CALL range from the module. Its 12-byte replacement is therefore
 * an absolute call. The adapter has precisely the stock copy routine's C ABI,
 * so it must be called directly: this constructor already owns a large stack
 * frame, and an all-register hook frame would overflow its stack. The span
 * includes the following stock P1 load; replay it before returning to the
 * next complete stock instruction. */
.section .text
.align 2
.extern sdk_extensions_system_entries

.global sdk_extensions_system_entries_hook
.type sdk_extensions_system_entries_hook, STT_FUNC
sdk_extensions_system_entries_hook:
    CALL sdk_extensions_system_entries
    P1.H = 0x8400;
    P1.L = 0xc0ec;
    RTS;
.size sdk_extensions_system_entries_hook, .-sdk_extensions_system_entries_hook
