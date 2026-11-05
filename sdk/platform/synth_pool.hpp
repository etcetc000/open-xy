// The voice pool of a synth registered at runtime (a module machine). It is
// voice.cpp.in's adapter made generic: the same handles, voice assignment,
// stealing, fades and silence detection, with the machine's callbacks and
// sizes taken from its description instead of being compiled in. Stock never
// sees a difference: it gets a 12-byte handle per voice index whose first word
// is the method table, exactly as from a built-in synth.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace synth {

// What a module machine hands over; the costume builder generates it from
// the machine's own types (callbacks wrap <namespace>::initialize etc.).
struct Machine {
    u32 voice_bytes, voice_alignment, shared_bytes, capacity;
    void (*initialize)(void* voice, void* shared, u32 id);
    void (*start)(void* voice, void* shared, u32 id);
    void (*destroy)(void* voice, void* shared, u32 id);
    void (*render)(void* voice, void* shared, u32 id, i32* output, const i32* controls, const u32* increments);
};

const u32 voice_indices = 256;  // stock's voice indices; every one gets a handle
const u32 stock_voices = 27;    // stock sounds at most 27 notes at once (8d51c994)
const u32 max_voices = stock_voices + 1;
const u32 young_blocks = 8;     // blocks stock spares a new voice for
const u32 silent_ticks = 60;    // about 20 ms of CPU without a counted block: every note has ended

struct Handle { const void* const* vtable; u16 seen, asked; u8 slot, wants; };
static_assert(sizeof(void*) != 4 || sizeof(Handle) == 12, "Handle layout");
struct Slot { unsigned char* voice; u32 owner, since, next; u16 born; };

struct Pool {
    const Machine* machine;
    const void* const* vtable;
    u32 (*ticks)();              // the voice clock (voice_clock.s)
    const u32* increments;       // stock's per-sample pitch increments
    Handle handles[voice_indices];
    Slot slots[max_voices];
    u32 voices;                  // min(capacity, stock_voices) + 1
    unsigned char* shared;
    u16 blocks;
    u32 counted;
};

// Storage for voices and shared state, aligned like the machine's types.
u32 storage_bytes(const Machine&);
// False for a description the pool cannot serve; storage holds storage_bytes.
bool setup(Pool&, const Machine*, const void* const* vtable, u32 (*ticks)(), const u32* increments,
           unsigned char* storage);
bool owns(const Pool&, const void* handle);

void* factory(Pool&, u32 index);
int start(Pool&, void* handle);
int destroy(Pool&, void* handle);
int render(Pool&, void* handle, i32* output, const i32* parameters);
}}
