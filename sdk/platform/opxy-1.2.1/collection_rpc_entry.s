/* Run the hold-only coordinator before the initializer's first instruction.
 * Save its complete incoming ABI context, then replay the exact stock prologue.
 */
.section .text
.include "sdk/platform/opxy-1.2.1/hook.inc"
.align 2
.extern sdk_collection_rpc_entry
.global sdk_collection_rpc_entry_hook
.type sdk_collection_rpc_entry_hook, STT_FUNC
sdk_collection_rpc_entry_hook:
    HOOK_SAVE
    CALL sdk_collection_rpc_entry;
    HOOK_RESTORE
    .short 0xe800, 0, 0x05fb, 0x6f46
    .short 0xdc00, 0x8d50, 0x67dc, 0
.size sdk_collection_rpc_entry_hook, .-sdk_collection_rpc_entry_hook
