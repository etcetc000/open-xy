#pragma once
#include "types.hpp"

namespace opxy { namespace live_input {

// The last three encoders on the first Shift page use these choice tables.
// Target starts at zero, choice one follows the Brain mask, and amount
// starts at its last choice. Captions must use printable text.
struct Choices { const char* const* labels; u32 count; };
struct Controls {
    const char* labels[3];
    Choices target, retune, amount;
    const char* first_page_hint;
    const char* second_page_hint;
};
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
