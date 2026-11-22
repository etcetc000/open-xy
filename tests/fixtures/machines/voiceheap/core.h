#pragma once
#include <opxy/audio.hpp>

namespace voiceheap {
// 4096 samples of echo (93 ms): nine voices take 147,564 bytes, more than the
// 128 KiB a synth keeps in module memory, so the SDK places them on the heap.
const opxy::u32 kEchoFrames = 4096;
struct Voice {
    opxy::u32 angle, write;
    opxy::i32 level;  // the sine's level, decaying from note start
    opxy::i32 echo[kEchoFrames];
};
struct Shared {};
}
