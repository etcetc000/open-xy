#pragma once
// Heap voices for a synth whose voice pool is too large for module memory
// (tooling/voice_heap.py, voice_heap_arena.cpp.in). The synth's adapter keeps
// one arena of its voices, reserved from the stock application heap the first
// time the synth registers on the audio role at boot; registering on the GUI
// role, or again, reserves nothing. The adapter's bookkeeping stays in module
// memory, and no note takes a voice until the arena exists, so a synth whose
// arena the heap refused stays silent and never asks again. Nothing here runs
// in an audio callback. Each adapter includes this once; its definitions have
// internal linkage, so several heap synths link side by side.
#include "stock.hpp"

extern "C" opxy::u32 stock_gui_role() __asm__("stock_gui_role");

namespace opxy { namespace platform { namespace voice_heap {
// An arena's status, as <prefix>_voice_heap_status reports it.
enum : u32 { kPending = 0, kReady = 1, kRefused = 2 };
// The heap an arena is reserved from: stock's role flag, whether the heap exists
// yet, and the heap's lock, allocate (null when exhausted) and unlock.
struct Heap {
    u32 (*gui_role)();
    u32 (*exists)();
    void (*lock)();
    void* (*allocate)(u32 bytes);
    void (*unlock)();
};
namespace {
// The application heap as sample analysis takes it (sample_cache_stock.cpp):
// its mutex, then its pool, allocating without stock's fatal error reporter.
const u32 kHeap = 0x08036aa4;
// Stock's malloc creates the mutex and pool on first use (8d4d0380); before that
// the mutex word is 0, and the arena is refused rather than lock handle 0.
u32 stock_exists() { return word(kHeap) != 0; }
void stock_lock() { stock<void(u32, i32)>(0x8c03e6f4)(word(kHeap), -1); }
void* stock_allocate(u32 bytes) { return stock<void*(u32, u32)>(0x8c048788)(word(kHeap + 4), bytes); }
void stock_unlock() { stock<void(u32, u32, u32, u32)>(0x8c03e9c8)(word(kHeap), 0, 0, 0); }
const Heap kStockHeap = {stock_gui_role, stock_exists, stock_lock, stock_allocate, stock_unlock};

template <class Voice, u32 Count> struct Arena {
    Voice* voices;  // Count voices, or null
    u32 status;
    // Reserves the voices while stock's role flag says audio, aligned and zeroed
    // as module memory starts; a heap stock has not created yet refuses. The
    // status is checked again under the heap's lock, so a second caller (even
    // one that misread the flag) never allocates; it returns the first one's status.
    u32 reserve(const Heap& heap) {
        static_assert(alignof(Voice) >= 4 && sizeof(Voice) % 4 == 0, "A voice is cleared in whole words");
        if (status != kPending || heap.gui_role()) return status;
        if (!heap.exists()) return status = kRefused;
        heap.lock();
        if (status == kPending) {
            const u32 bytes = sizeof(Voice) * Count + alignof(Voice) - 1;
            void* const block = heap.allocate(bytes);
            if (block) {
                typedef __UINTPTR_TYPE__ Address;
                const Address mask = alignof(Voice) - 1;
                typedef u32 __attribute__((may_alias)) Word;
                Word* const words = reinterpret_cast<Word*>((reinterpret_cast<Address>(block) + mask) & ~mask);
                for (u32 i = 0; i < sizeof(Voice) * Count / 4; ++i) words[i] = 0;
                __asm__ volatile("" ::: "memory");  // the zeroed voices before the pointer and status that publish them
                voices = reinterpret_cast<Voice*>(words);
            }
            status = block ? kReady : kRefused;
        }
        heap.unlock();
        return status;
    }
};
}
} } }
