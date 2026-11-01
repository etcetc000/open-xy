#include "collection_cache.hpp"
namespace opxy { namespace collection { namespace activation {
bool qualify_cache(CacheOps ops, volatile u16* scratch, u32 address, u32 base, u32 bytes) {
    if (!scratch || !ops.synchronize || !ops.invoke || (address & 31) || (base & 31) ||
        bytes<32 || bytes>0xffffffffu-base || address<base || address-base>bytes-32) return false;
    const u16 expected[16]={0xe120,0x1357,0x0010};
    for (u32 i=0;i<16;++i) if (scratch[i]!=expected[i]) return false;
    if (ops.invoke(ops.context)!=0x1357) return false;
    scratch[1]=0x2468;
    const bool synchronized=ops.synchronize(ops.context,address,32);
    const bool changed=synchronized && ops.invoke(ops.context)==0x2468;
    // Restore even when qualification failed. A failed test never authorizes
    // code replacement, and no other component calls this isolated scratch.
    scratch[1]=0x1357;
    const bool restored=ops.synchronize(ops.context,address,32);
    return changed && restored && ops.invoke(ops.context)==0x1357;
}
}}}
