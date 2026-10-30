#pragma once
#include <opxy/audio.hpp>

namespace probesynth {
struct Voice { opxy::u32 phase; };
// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
