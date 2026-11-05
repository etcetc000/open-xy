#pragma once
#include "page_header.hpp"

namespace opxy { namespace draw {
// Recommended page for a send effect with eight controls (SDK 1.10), drawn as
// stock's FX page (generic_fx2) is: stock's badge naming the send (FX I or
// FX II) beside the effect's name, at stock's size and place, then both pages
// as rows of four 100 x 43 px columns, each a title above, a box with a level
// bar and a caption below. The first row's titles sit where stock's do, 5 px
// above their boxes as stock's, and
// the rows are stock's 65 px row scaled to two-thirds, text excepted.
// One white band, the full width of the screen, sits behind the row the
// encoders edit and slides to the other row on a press; its ends carry the
// send's input and return meters, stock's thin lines from the band's middle
// row moving with the signed level, down or up. The boxes stay put. On the edited row they
// are stock's: black, #323232 and #969696, the fourth column being the band
// itself, each with stock's bar, 5 px tall, #CDCDCD on the first and black on
// the others. The other row shows the same at the dim text's brightness. Every
// bound shape keeps its authored fill, unstroked, on whole pixels: a press
// moves the bright and dim sets between the rows rather than repainting them.
// Pair with the authored block of the machine template's myeffect8 panel.svg.fragment.
// Store PageHeader in DisplayState and reset() it in display_reset.
//
// Nodes from first_node: the band; the bright row's three boxes and the dim
// row's four; the bright row's four bars and the dim row's; the input and the
// return meter. Text slots 0..15 as the two-page header uses them, and 16 the
// badge's numeral. The page takes no paths and paints no palette entry.
const u32 effect_page_nodes = 18, effect_page_texts = 17;
const u32 effect_page_second_send = 15;  // input.track of send B; send A is 14

namespace effect_page_detail {
const i32 left = 40, column = 100, band_height = 43, bar_height = 5;
// Box tops: titles 5 px above, as stock's, the first row's on stock's
// baseline at 75; captions 11 px below the box.
const i32 row_top[2] = {80, 156}, title_rise = 5, caption_drop = band_height + 11;
const u32 bright_boxes = 1, dim_boxes = 4, bright_bars = 8, dim_bars = 12, meters = 16, numeral = 16;
// The badge's numeral as stock's fx1 / fx2 place it, in Q10: I or II, on a 47.5 px baseline.
const i32 numeral_x[2] = {224922, 221952}, numeral_y = 48640;
// A meter line's full drop: stock's 70 px over its 65 px band, at this band's scale.
const i32 meter_reach = 46;
// A bar's top in whole pixels below its box's top, as stock 1.2.1 draws it on
// its 65 px box (floor(57 (1 - value))), scaled to this box: floor(38 (1 - value)),
// so 0 at full scale and 38 at 0.
static inline i32 bar_offset(i32 value) {
    const u32 rest = (0x80000000u - static_cast<u32>(value < 0 ? 0 : value)) >> 8;  // 2^23 at 0
    return static_cast<i32>((band_height - bar_height) * rest >> 23);
}
// A meter line's offset in whole pixels from the band's middle row, as stock
// 1.2.1 moves its vu_in and vu_out lines, scaled: trunc(46 x level) for a
// signed Q31 level, so down 23 at +0.5 and up 23 at -0.5, truncated toward the
// middle. A loud send takes it past the band's edge.
static inline i32 meter_drop(i32 level) {
    const u32 size = level < 0 ? 0u - static_cast<u32>(level) : static_cast<u32>(level);
    const i32 pixels = static_cast<i32>(meter_reach * (size >> 8) >> 23);
    return level < 0 ? -pixels : pixels;
}
}

// titles and values are indexed by saved parameter slot, as for page_header.
// Captions are 00..99 (header_number) or a choice's name, as stock's FX page shows.
// slots maps page/column to parameter slot when alternates are not in column order.
template <class F>
static inline void effect_page(PageHeader& state, const Input& input, F& frame,
                               const char* const (&titles)[8], const char (&values)[8][header_capacity],
                               u32 first_node, const u8 (&slots)[2][4] = page_header_slots) {
    static_assert(F::text_count >= effect_page_texts, "Effect page needs 17 text slots");
    static_assert(F::node_count >= effect_page_nodes, "Effect page needs 18 nodes");
    using namespace effect_page_detail;
    const u32 page = page_motion(state, input, slots);
    const i32 p = state.position;
    const u32 bright = p >= unit / 2 ? 1 : 0;
    // The band and its meters move together in whole pixels.
    const i32 band = floor_q10(page_header_detail::mix(row_top[0] * unit, row_top[1] * unit, p));
    place(frame, first_node, 0, band * unit, unit, 0, keep);
    const i32 levels[2] = {input.level_in, input.level_out};
    for (u32 m = 0; m < 2; ++m)
        place(frame, first_node + meters + m, (m ? left + 4 * column : 0) * unit,
              (band + band_height / 2 + meter_drop(levels[m])) * unit, unit, 0, keep);
    for (u32 row = 0; row < 2; ++row) {
        const i32 top = row_top[row];
        const bool lit = row == bright;
        const u32 title_slot = lit ? 0 : 8, value_slot = lit ? 4 : 12;
        for (u32 col = 0; col < 4; ++col) {
            const u32 slot = slots[row][col];
            const i32 x = left + column * static_cast<i32>(col);
            if (col < 3 || !lit) place(frame, first_node + (lit ? bright_boxes : dim_boxes) + col, x * unit, top * unit,
                                       unit, 0, keep);
            place(frame, first_node + (lit ? bright_bars : dim_bars) + col, x * unit,
                  (top + bar_offset(input.slots[slot])) * unit, unit, 0, keep);
            char title[text_capacity];
            page_header_detail::lowercase(title, titles[slot]);
            text(frame, title_slot + col, x * unit, (top - title_rise) * unit, unit, title);
            text(frame, value_slot + col, x * unit, (top + caption_drop) * unit, unit, values[slot]);
        }
    }
    const u32 second = input.track == effect_page_second_send ? 1 : 0;
    text(frame, numeral, numeral_x[second], numeral_y, unit, second ? "II" : "I");
    for (u32 col = 0; col < 4; ++col) header(frame, col, values[slots[page][col]]);
}
}} // namespace opxy::draw
