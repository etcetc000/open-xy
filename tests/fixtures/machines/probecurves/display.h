#pragma once
#include <opxy/encoder_activity.hpp>
namespace probecurves {
struct DisplayState { opxy::u32 serial; };
using Frame = opxy::draw::AffineQuadraticFrame<2, 16, 2, 0, 0>;
}
