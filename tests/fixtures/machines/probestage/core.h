#pragma once
#include <opxy/audio.hpp>

namespace probestage {
struct Voice { opxy::u32 phase; };
struct Shared {};
// The track stage's state, one per instrument track: the right channel's last
// 512 frames, and where the next one goes.
const opxy::u32 kLineFrames = 512;
struct TrackState {
    opxy::i32 line[kLineFrames];
    opxy::u32 write;
};
}
