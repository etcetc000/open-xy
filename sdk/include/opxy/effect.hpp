#pragma once
#include "audio.hpp"
#include "transport.hpp"

namespace opxy {
// Send effects process one 64-frame stereo block in place:
//   struct State { ... };
//   void initialize(State& state);
//   void process(State& state, opxy::Channel left, opxy::Channel right,
//                opxy::Parameters parameters);
// Stock constructs the effect inside a 480-byte slot behind a one-word object
// header and never destroys it, so State holds no resources. initialize runs
// after construction and again on an engine reset: keep it short.
//
// An effect that declares resources.bufferBytes (SDK 1.2) takes a buffer too:
//   void process(State& state, opxy::Buffer buffer, opxy::Channel left,
//                opxy::Channel right, opxy::Parameters parameters);
// The buffer is stock's per-send scratch memory: it outlives the effect and is
// never cleared, so read only what this effect wrote since initialize. Outside
// a send slot it is empty (count 0); then write silence. Its State is at most
// effect_buffered_state_bytes, since the slot also holds the buffer's address.
//
// An effect that declares resources.transport 1 (SDK 1.2) also takes the
// tempo, read once per block, after the buffer if it has one:
//   void process(State& state, [opxy::Buffer buffer,] const opxy::Transport& transport,
//                opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
//
// A control with display "division" picks one of stock delay's eight note
// lengths, an eighth of its travel each; the send page shows its name.
// process writes only the wet return. Optional machine.json dryParameter names
// a normalized control that the native adapter exposes to the stock dry mixer;
// omit it to preserve the source dry level. Do not add dry audio in process.
using Channel = i32* __restrict;
const unsigned effect_state_bytes = 476;
const unsigned effect_buffered_state_bytes = 472;
const unsigned effect_state_alignment = 4;
const unsigned effect_buffer_bytes = 524288;  // at most, per send slot
// Stock delay's note lengths, in sixteenths of a quarter note, by control >> 28:
// 1/32, 1/32 dotted, 1/16, 1/16 dotted, 1/8, 1/8 dotted, 1/4 and 1/2.
const u32 division_sixteenths[8] = {2, 3, 4, 6, 8, 12, 16, 32};
inline u32 division(i32 control) { return control < 0 ? 0 : static_cast<u32>(control) >> 28; }
struct Buffer {
    i32* words;   // null outside a send slot
    u32 count;    // words: bufferBytes / 4, or 0
};
}
