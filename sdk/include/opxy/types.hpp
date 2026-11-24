#pragma once

namespace opxy {
// Compiler-provided widths avoid depending on a target C library.
using i32 = __INT32_TYPE__;
using u32 = __UINT32_TYPE__;
using u16 = __UINT16_TYPE__;
using i16 = __INT16_TYPE__;
using u8 = __UINT8_TYPE__;
using i8 = __INT8_TYPE__;
using i64 = __INT64_TYPE__;
static_assert(sizeof(i32) == 4 && sizeof(u32) == 4, "32-bit audio words required");
// Q10 fixed point, used by display coordinates, scales and unit vectors: 1.0 is 1024.
const i32 q10_unit = 1024;
// Controls on the stock four-knob page; display inputs carry one word for each.
const unsigned page_controls = 4;
// The stock parameter vector: a synth's four controls, then their alternates.
const unsigned parameter_slots = 8;
}
