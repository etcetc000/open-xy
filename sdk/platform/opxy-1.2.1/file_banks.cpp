// File banks on stock OS 1.2.1: newlib file access and the stock application
// heap, both as stock itself calls them. Runs from engine registration at
// startup, once per role, never from an audio callback. Device-only: host tests
// drive file_bank_loader.hpp with their own backend.
#include "file_banks.hpp"
#include "barrier.hpp"
#include "stock.hpp"

extern "C" opxy::u32 stock_gui_role() __asm__("stock_gui_role");

namespace {
using namespace opxy;
using opxy::platform::Address;
using opxy::platform::stock;
using opxy::platform::word;

// The stock application heap (sample_cache_stock.cpp): its mutex, then its pool.
const u32 kHeap = 0x08036aa4;
// The pool's arena on OS 1.2.1, and its allocator's layout: a 24-byte prefix,
// a 480-byte control chunk and an 88-byte footer.
const u32 kArena = 0x8c0d0380, kArenaBytes = 0x1200000, kPrefix = 24, kControl = 480, kFooter = 88;

struct Backend {
    // newlib, as stock's project reader calls it (project_files.hpp).
    void* open(const char* path) { return stock<void*(const char*, const char*)>(0x8fed0348)(path, "rb"); }
    // The first halfword of a FILE holds its flags: stock fread sets bit 9 on a
    // read error (8fed06ae, 8fed1f1a) and bit 8 at the end (8fed1f28). A failed
    // trailing read is not the end of the file.
    i32 read(void* file, void* data, u32 bytes) {
        const u32 count = stock<u32(void*, u32, u32, void*)>(0x8fed05bc)(data, 1, bytes, file);
        const u32 flags = *static_cast<const volatile u16*>(file);
        if (count > bytes || (flags & 0x200u) || (!count && !(flags & 0x100u))) return -1;
        return static_cast<i32>(count);
    }
    bool close(void* file) { return stock<i32(void*)>(0x8fed00ae)(file) == 0; }
    // 8d4d03fc's lock, allocate and unlock, returning null on exhaustion instead
    // of calling its fatal error reporter (as sdk_sample_cache_alloc does).
    void* allocate(u32 bytes) {
        if (!word(kHeap) || word(kHeap + 4) != kArena + 32) return nullptr;
        if (stock<i32(u32, i32)>(0x8c03e6f4)(word(kHeap), -1) != 1) return nullptr;
        void* const block = stock<void*(u32, u32)>(0x8c048788)(word(kHeap + 4), bytes);
        stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(word(kHeap), 0, 0, 0);
        return block;
    }
    // A block the pool handed out: inside its arena, marked in use, and sized
    // for the request. Anything else is never touched.
    bool valid(const void* block, u32 bytes) {
        const Address a = reinterpret_cast<Address>(block);
        const u32 low = kArena + kPrefix + kControl + 8, end = kArena + kArenaBytes - kFooter;
        if (a % 32 || a < low || a >= end || bytes > end - a) return false;
        const u32 header = word(a - 4), chunk = header & ~7u;
        return (header & 2) && !(chunk % 32) && chunk >= bytes + 4 && chunk <= bytes + 64 && chunk <= end - (a - 8);
    }
    void release(void* block) { stock<void(void*)>(0x8d4d2910)(block); }
    void publish_barrier() { opxy::platform::barrier(); }
};

file_banks::Loader<Backend> loader;
bool prepared;
}

// heapState (tooling/adapters.py): one zeroed block for a machine's voice pool
// and shared state, taken at startup and never released; null if the heap has
// no room or hands back an unexpected block, and the machine stays silent.
void* sdk_boot_allocate(u32 bytes) {
    Backend io;
    if (!bytes) return nullptr;
    void* const block = io.allocate(bytes);
    if (!block) return nullptr;
    if (!io.valid(block, bytes)) return nullptr;  // never touched, never freed
    unsigned char* const data = static_cast<unsigned char*>(block);
    for (u32 i = 0; i < bytes; ++i) data[i] = 0;
    io.publish_barrier();
    return block;
}

int sdk_file_banks_prepare() {
    if (prepared) return 1;
    prepared = true;
    if (stock_gui_role() && !opxy::platform::native_file_banks_gui) return 1;
    Backend io;
    u32 charged = 0;
    for (u32 f = 0; f < opxy::platform::native_file_bank_count; ++f) {
        loader = file_banks::Loader<Backend>();
        if (loader.load(io, opxy::platform::native_file_banks[f], charged, opxy::platform::native_file_bank_budget) !=
            file_banks::prepared)
            continue;
        for (u32 u = 0; u < opxy::platform::native_file_bank_use_count; ++u)
            if (opxy::platform::native_file_bank_uses[u].file == f)
                loader.publish(io, *opxy::platform::native_file_bank_uses[u].view);
    }
    return 1;
}
