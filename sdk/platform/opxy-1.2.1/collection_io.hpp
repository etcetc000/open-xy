#pragma once
#include "collection_resources.hpp"
namespace opxy { namespace collection { namespace io {
struct Heap {
    u32 protected_base, protected_bytes, arena_base, arena_bytes, reserve_bytes;
};
activation::Allocator allocator(Heap&);
unsigned char* try_allocate(Heap&,u32 bytes);
u32 contiguous_free(Heap&); // Conservative topchunk lower bound; zero also means unavailable.
resources::Files files();
typedef bool (*Cancelled)(void*);
bool read_bytes(const char* path, unsigned char*, u32 bytes, bool& opened,
                Cancelled=nullptr,void* context=nullptr);
// GUI helper only. fclose's FAT CTRL_SYNC discards the card-flush result;
// explicitly check the role-aware stock flush before reopening/readback.
// This is not a claim of arbitrary-power-loss durability.
bool persist_attempt(const activation::Control&,const activation::Limits&,
                     Cancelled=nullptr,void* context=nullptr);
u32 ticks();
void delay(u32);
bool gui();
}}}
