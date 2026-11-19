#pragma once
#include "types.hpp"

namespace opxy { namespace live_input {

// Rows 0–2 are encoders 2–4 on page one. Rows 3–6 are encoders
// 1–4 on page two. An empty row has no label or choices.
constexpr u32 first_rows = 3, second_rows = 4, row_count = first_rows + second_rows;
constexpr u32 choice_bits = 18, max_choices = 32;
struct Row { const char* label; const char* const* labels; u32 count, initial; };
struct Controls {
    Row rows[row_count];
    const char* first_page_hint;
    const char* second_page_hint;
};
struct Parameters {
    u32 choices[row_count]; // indices in Controls row order
    u32 brain; // current pitch-class mask, or zero when unavailable
};

constexpr u32 width(u32 count) {
    return count <= 1 ? 0 : 1 + width((count + 1) / 2);
}
constexpr u32 caption_bytes(const char* caption, u32 size = 0) {
    return !caption || size == 32 || !caption[size] ? size : caption_bytes(caption, size + 1);
}
constexpr bool choices_fit(const Row& row, u32 prefix, u32 i = 0) {
    return i == row.count || (row.labels[i] && prefix + caption_bytes(row.labels[i]) <= 31 &&
                              choices_fit(row, prefix, i + 1));
}
constexpr bool row_valid(const Row& row, const char* hint) {
    return !row.count ? !row.label && !row.labels && !row.initial :
        row.label && row.labels && row.count <= max_choices && row.initial < row.count &&
        choices_fit(row, caption_bytes(row.label) + 3 + (hint ? 3 + caption_bytes(hint) : 0));
}
constexpr bool has_rows(const Controls& controls, u32 from, u32 end) {
    return from < end && (controls.rows[from].count || has_rows(controls, from + 1, end));
}
constexpr bool rows_valid(const Controls& controls, u32 i, u32 remaining) {
    return i == row_count || (row_valid(controls.rows[i], i == first_rows - 1 ? controls.second_page_hint :
        i == row_count - 1 ? controls.first_page_hint : nullptr) &&
        width(controls.rows[i].count) <= remaining &&
        rows_valid(controls, i + 1, remaining - width(controls.rows[i].count)));
}
constexpr bool valid_controls(const Controls& controls) {
    return has_rows(controls, 0, first_rows) && rows_valid(controls, 0, choice_bits);
}

}}
