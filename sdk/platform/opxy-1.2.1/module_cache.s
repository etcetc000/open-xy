/* Makes freshly relocated module code visible to the core: data cache lines
 * written back, then instruction cache lines invalidated, over one range.
 * From #339's collection_cache.s (sdk_collection_sync_code), unchanged.
 * BF70x Programming Reference, CacheCtrl and Sync (8-155..8-158):
 * https://www.analog.com/media/en/dsp-documentation/processor-manuals/ADSP-BF70x_Blackfin_Programming_Reference.pdf
 * R0/R1 describe an aligned, nonempty range. No cache mode, MMR, interrupt
 * mask, stack or callee-saved register is changed. Returns 1 when the range
 * operations completed, 0 for an invalid range.
 */
.section .text
.align 2
.global sdk_module_sync_code
.type sdk_module_sync_code, STT_FUNC
sdk_module_sync_code:
    R2 = R0 | R1;
    R3 = 31;
    R2 = R2 & R3;
    CC = R2 == 0;
    IF !CC JUMP .Lcache_invalid;
    CC = R1 == 0;
    IF CC JUMP .Lcache_invalid;
    R3 = R0 + R1;
    CC = R3 < R0 (IU);
    IF CC JUMP .Lcache_invalid;
    P0 = R0;
    P1 = R0;
    R2 = R1;
    R2 >>= 5;
    SSYNC;
.Lcache_data:
    FLUSH [P0++];
    R2 += -1;
    CC = R2 == 0;
    IF !CC JUMP .Lcache_data;
    SSYNC;
    R1 >>= 5;
.Lcache_instruction:
    IFLUSH [P1++];
    R1 += -1;
    CC = R1 == 0;
    IF !CC JUMP .Lcache_instruction;
    SSYNC;
    CSYNC;
    R0 = 1;
    RTS;
.Lcache_invalid:
    R0 = 0;
    RTS;
.size sdk_module_sync_code, .-sdk_module_sync_code
