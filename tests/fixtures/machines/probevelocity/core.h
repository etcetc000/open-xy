#pragma once
#include <opxy/audio.hpp>

namespace probevelocity {
struct Voice { opxy::u32 velocity; };
// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
