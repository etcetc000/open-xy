#pragma once
#include <opxy/types.hpp>

namespace probesources {
// A triangle from a 32-bit phase, at half scale. Compiled as its own
// translation unit (machine.json "sources") and found through "includes".
opxy::i32 triangle(opxy::u32 phase);
}
