#pragma once
#include <opxy/audio.hpp>

namespace probelarge {
struct Voice { opxy::u32 position; };
// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
