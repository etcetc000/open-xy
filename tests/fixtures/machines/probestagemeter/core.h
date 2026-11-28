#pragma once
#include <opxy/audio.hpp>
#include <opxy/telemetry.hpp>

namespace probestagemeter {
struct Voice { opxy::u32 phase; };
struct Shared {};
// The track stage's state, one per instrument track: the louder channel's held peak.
struct TrackState { opxy::u32 held; };
}
