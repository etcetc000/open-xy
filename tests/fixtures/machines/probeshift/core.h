#pragma once
#include <opxy/audio.hpp>

namespace probeshift {
struct Voice { opxy::u32 phase, noise; };
// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
