/* Replay the three complete instructions displaced by the absolute entry
 * jump. Both directions exceed the relative branch range. */
.section .text
.align 2
.global sdk_collection_rpc_stock
.type sdk_collection_rpc_stock, STT_FUNC
sdk_collection_rpc_stock:
    LINK 0;
    [--SP] = (R7:6, P5:5);
    R7 = R0;
    .short 0xdc00, 0x8c0c, 0x2374, 0
.size sdk_collection_rpc_stock, .-sdk_collection_rpc_stock
