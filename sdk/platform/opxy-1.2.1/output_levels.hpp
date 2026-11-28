#pragma once
#include <opxy/output_levels.hpp>
#include "stock.hpp"
namespace opxy { namespace platform {
// Stock's level table on the GUI processor, which its meters read: entries 0
// and 1 are the output's left and right, entry 2 the selected input. Each entry
// is the latest word the audio processor sent, not one atomic frame. Read only.
inline draw::OutputLevels output_levels() {
    draw::OutputLevels result = {};
    if (!gui_role()) return result;
    const volatile i32* table = at<const volatile i32>(0x080376e4u);
    result.available = 1;
    result.output_left = table[0];
    result.output_right = table[1];
    result.input = table[2];
    return result;
}
}}
