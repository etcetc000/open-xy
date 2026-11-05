// The fake synth's callbacks, included by both adapters like a machine's engine.cpp.
#pragma once
#include <opxy/audio.hpp>
namespace fake {
inline void note(char op, opxy::u32 id, const Voice& v) {
    if (logged < sizeof(log) / sizeof(log[0])) log[logged++] = Event{op, id, v.starts, v.renders};
}
inline void initialize(Voice& v, Shared&, opxy::u32 id) { v.id = id; note('i', id, v); }
inline void start(Voice& v, Shared&, opxy::u32 id) { ++v.starts; note('s', id, v); }
inline void destroy(Voice& v, Shared&, opxy::u32 id) { note('d', id, v); }
inline void render(Voice& v, Shared&, opxy::u32 id, opxy::i32* out, const opxy::i32* controls, const opxy::u32*) {
    ++v.renders;
    note('r', id, v);
    for (opxy::u32 i = 0; i < opxy::block_frames; ++i)
        out[i] = static_cast<opxy::i32>((id << 20) | (v.starts << 12) | (v.renders & 0xfff)) + controls[0];
}
}
