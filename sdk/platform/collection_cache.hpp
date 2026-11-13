// Qualification touches only a dedicated resident scratch line, never payload.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace collection { namespace activation {
struct CacheOps {
    void* context;
    bool (*synchronize)(void*,u32 address,u32 bytes);
    u32 (*invoke)(void*);
};
bool qualify_cache(CacheOps, volatile u16* scratch, u32 address, u32 resident_base, u32 resident_bytes);
}}}
