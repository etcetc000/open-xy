// A fake synth for the pool equivalence test: every callback is logged, and
// each output sample encodes which voice rendered and how often.
#pragma once
#include <opxy/types.hpp>
namespace fake {
struct Voice { opxy::u32 starts, renders, id, pad[5]; };
struct Shared {};
struct Event { char op; opxy::u32 id, starts, renders; };
extern Event log[1 << 16];
extern opxy::u32 logged;
}
