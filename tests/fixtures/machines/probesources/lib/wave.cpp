#include "wave.h"

namespace probesources {
opxy::i32 triangle(opxy::u32 phase) {
    opxy::u32 folded = phase & 0x7fffffffu;
    if (phase & 0x80000000u) folded = 0x7fffffffu - folded;
    return static_cast<opxy::i32>(folded) - 0x40000000;
}
}
