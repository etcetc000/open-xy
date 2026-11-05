#pragma once
#include "stock.hpp"
namespace opxy { namespace platform { namespace input_monitor_popup {
// Private instance of stock's fx_send.svg presenter (page 22). The base is
// 24 bytes; four value leaves, row groups, palette IDs, icons and float motion
// states follow it. Stock owns its document and bindings, never project data.
struct Node {
    u32 vtable;
    i32 matrix[9];
    u8 stock40[52];
    u8 visible, stock93[3]; // zero skips drawing in the stock node renderer
};
struct Popup {
    const void* vtable;
    const char* asset;
    const void* svg;
    u32 page;
    const void* loader;
    u32 flags;
    Node* values[4];
    Node* rows[4];
    u16 palette[4];
    Node* icons[4];
    u32 motion[4];
};
static_assert(__builtin_offsetof(Node, visible) == 92 && sizeof(Node) == 96, "Stock SVG node visibility");
static_assert(sizeof(void*) != 4 || (sizeof(Popup) == 96 &&
              __builtin_offsetof(Popup, values) == 24 && __builtin_offsetof(Popup, rows) == 40 &&
              __builtin_offsetof(Popup, palette) == 56 && __builtin_offsetof(Popup, icons) == 64 &&
              __builtin_offsetof(Popup, motion) == 80), "Stock send popup layout");
// Stock's bounded +/-24 px binary32 slide, converted to Q10 toward zero.
// The stock frame advances the actual animation; this only recenters its row.
inline i32 motion_q10(u32 bits) {
    const u32 exponent = (bits >> 23) & 255u;
    if (exponent < 117 || exponent > 132) return 0;
    const i32 amount = static_cast<i32>(((bits & 0x7fffffu) | 0x800000u) >> (140 - exponent));
    return bits & 0x80000000u ? -amount : amount;
}
}}}
