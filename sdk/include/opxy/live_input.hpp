#pragma once
#include "types.hpp"

namespace opxy { namespace live_input {

// Shift on Aux 5 M1 shows target, retune and the shared 0–100% amount.
// Target starts at 0 (off). Target 1 follows the Brain mask. Retune starts
// at 0 and offers two choices. Values use printable captions of at most 12 bytes.
struct Choices { const char* const* labels; u32 count; };
struct Controls { Choices target, retune; };
constexpr u32 max_target = 32, max_amount = 15;

// Optional second Shift page. Each encoder starts at choice zero.
constexpr u32 extra_rows = 4;
constexpr u32 extra_capacity[extra_rows] = {8, 8, 2, 2};
struct ExtraControls {
    const char* labels[extra_rows];
    Choices choices[extra_rows];
};
struct ExtraValues { u32 values[extra_rows]; };

}}
