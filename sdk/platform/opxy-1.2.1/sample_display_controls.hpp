#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
// A raw key is owned before stock held bookkeeping. Its release remains ours
// even if the display/controller changes while the key is held.
struct SampleDisplayKey {
    bool owned;
    bool consume(u32 key, bool down, bool eligible) {
        if (key != 6) return false;
        if (owned) { if (!down) owned = false; return true; }
        if (!down || !eligible) return false;
        owned = true;
        return true;
    }
};
// Matches the relocated screen table in tooling/stock_tables.py. Dynamic
// sample screens allocate from its high end, above the arranger's old range.
inline bool sample_display_page_valid(u32 page) { return page >= 82 && page < 256; }
bool sample_display_capable(u32 engine);
bool sample_display_active(u32 engine);
}}
extern "C" {
bool sdk_sample_display_key_event(const void*) __asm__("sdk_sample_display_key_event");
opxy::u32 sdk_sample_display_page(void*) __asm__("sdk_sample_display_page");
opxy::u32 sdk_sample_parameter_kind(opxy::u32) __asm__("sdk_sample_parameter_kind");
opxy::u32 sdk_sample_controller_engine(opxy::u32) __asm__("sdk_sample_controller_engine");
}
