#pragma once
#include <opxy/draw.hpp>
namespace probelarge {
struct DisplayState { opxy::u32 position; };
using Frame = opxy::draw::CubicFrame<6, 4, 6, 0, 4>;
}
