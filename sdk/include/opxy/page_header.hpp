#pragma once
#include "draw.hpp"

namespace opxy { namespace draw {
// Fixed two-row synth header (SDK 1.4). Pair with the artwork emitted by
// sdk.py new --controls 8: eight consecutive bar nodes and text slots 0..15.
// Store this state in DisplayState; reset() also works on uninitialized storage.
struct PageHeader {
    u32 initialized, track, page;
    i32 position, elapsed;
    void reset() { initialized = 0; }
};
const u32 page_header_nodes = 8, page_header_texts = 16;
const u8 page_header_slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};

// Rounded 0..100 caption without a percent sign. The underlying Q31 is unchanged.
static inline void header_amount(char* out, u32 capacity, i32 value) {
    const u32 n = value < 0 ? 0 : ((static_cast<u32>(value) >> 10) * 100 + (1u << 20)) >> 21;
    char number[4];
    u32 at = 0;
    if (n >= 100) number[at++] = '1';
    if (n >= 10) number[at++] = static_cast<char>('0' + n / 10 % 10);
    number[at++] = static_cast<char>('0' + n % 10);
    number[at] = 0;
    copy(out, capacity, number);
}

namespace page_header_detail {
static inline u32 remaining(u32 time) {
    // Exponential ease: halve the distance every 21 ms; settle after 0.15 s.
    const u32 table[9] = {65536, 60097, 55109, 50535, 46341, 42495, 38968, 35734, 32768};
    const u32 octaves = time * 1496, whole = octaves >> 15;
    const u32 eighth = (octaves >> 12) & 7, fraction = octaves & 4095;
    return (table[eighth] - (((table[eighth] - table[eighth + 1]) * fraction) >> 12)) >> whole;
}
static inline i32 mix(i32 a, i32 b, i32 position) { return a + floor_q10((b - a) * position); }
static inline void lowercase(char* out, const char* title) {
    copy(out, text_capacity, title);
    for (u32 i = 0; out[i]; ++i)
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] += 'a' - 'A';
}
}

// titles and values are indexed by saved parameter slot, never by the current
// encoder selection. Supply real-unit captions; use header_amount for amounts.
// slots maps page/column to parameter slot when alternates are not in column order.
// Other artwork belongs below y=38. No audio state or drawing ABI changes.
template <class F>
static inline void page_header(PageHeader& state, const Input& input, F& frame,
                        const char* const (&titles)[8], const char (&values)[8][header_capacity],
                        u32 first_node, const u8 (&slots)[2][4] = page_header_slots) {
    static_assert(F::text_count >= page_header_texts, "Two-page header needs 16 text slots");
    static_assert(F::node_count >= page_header_nodes, "Two-page header needs eight bar nodes");
    u32 page = 0;
    for (u32 col = 0; col < 4; ++col)
        if (slots[1][col] != slots[0][col] && input.shown[col] == slots[1][col]) page = 1;
    const i32 target = page ? unit : 0, settle = 154;
    if (!state.initialized || state.track != input.track || input.reduced_motion) {
        state.position = target;
        state.elapsed = settle;
    } else if (state.page != page) {
        // Reverse from the current position, including presses during a transition.
        state.elapsed = 0;
    } else if (state.elapsed < settle) {
        const i32 dt = clamp(input.elapsed, 0, settle);
        state.elapsed += dt;
        state.position = state.elapsed >= settle ? target
            : target + floor_q10((state.position - target) *
                                static_cast<i32>(page_header_detail::remaining(dt) >> 6));
    }
    state.initialized = 1;
    state.track = input.track;
    state.page = page;
    const i32 p = state.position;
    const u32 bright = p >= unit / 2 ? 1 : 0;
    for (u32 i = 0; i < page_header_nodes; ++i)
        place(frame, first_node + i, i * 60 * unit, 18 * p, unit, 0, keep);
    for (u32 row = 0; row < 2; ++row) {
        const i32 title_y = page_header_detail::mix((row ? 33 : 10) * unit, (row ? 28 : 13) * unit, p);
        const i32 value_y = page_header_detail::mix(row ? 33 * unit : 17818, row ? 36250 : 13 * unit, p);
        const i32 size = page_header_detail::mix(row ? 666 : unit, row ? unit : 666, p);
        const u32 title_slot = row == bright ? 0 : 8, value_slot = row == bright ? 4 : 12;
        const i32 scale = row == bright ? size : floor_q10(size * 1575);
        for (u32 col = 0; col < 4; ++col) {
            const u32 slot = slots[row][col];
            char title[text_capacity];
            page_header_detail::lowercase(title, titles[slot]);
            text(frame, title_slot + col, (5 + 120 * col) * unit, title_y, unit, title);
            text(frame, value_slot + col, (65 + 120 * col) * unit, value_y, scale, values[slot]);
        }
    }
    for (u32 col = 0; col < 4; ++col) header(frame, col, values[slots[page][col]]);
}
}} // namespace opxy::draw
