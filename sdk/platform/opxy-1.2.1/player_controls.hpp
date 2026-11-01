// Player enum editing uses the encoder's discrete detents, without the stock
// Maestro controller's doubled bucket count or continuous acceleration.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
inline i32 player_enum_turn(i32 value, u32 choices, i32 detents) {
    if (choices < 2 || choices > 255 || !detents) return value;
    const u32 positive = value < 0 ? 0u : static_cast<u32>(value);
    i32 bucket = static_cast<i32>(((positive >> 16) * choices) >> 15);
    if (bucket >= static_cast<i32>(choices)) bucket = choices - 1;
    const i32 last = static_cast<i32>(choices - 1);
    if (detents > 0) bucket = detents >= last - bucket ? last : bucket + detents;
    else bucket = detents <= -bucket ? 0 : bucket + detents;
    const u32 width = 0x7fffffffu / choices;
    return static_cast<i32>(static_cast<u32>(bucket) * width + width / 2);
}
}}
