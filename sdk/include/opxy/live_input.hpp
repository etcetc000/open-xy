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
// A stock popup row has 31 bytes for label, value, spacing and optional hint.
constexpr u32 caption_bytes(const char* caption, u32 size = 0) {
    return !caption || size == 32 || !caption[size] ? size : caption_bytes(caption, size + 1);
}

constexpr bool choice_captions_fit(const Choices& choices, u32 prefix, u32 i = 0) {
    return i == choices.count || (choices.labels[i] && prefix + caption_bytes(choices.labels[i]) <= 31 &&
                                  choice_captions_fit(choices, prefix, i + 1));
}

constexpr bool captions_fit(const char* label, const Choices& choices, const char* hint = nullptr) {
    return label && choices.labels &&
        choice_captions_fit(choices, caption_bytes(label) + 3 + (hint ? 3 + caption_bytes(hint) : 0));
}

constexpr bool valid_controls(const Controls& controls) {
    return controls.target.count >= 2 && controls.target.count <= max_target &&
        controls.retune.count && controls.retune.count <= 2 &&
        controls.amount.count && controls.amount.count <= max_amount + 1 &&
        captions_fit(controls.labels[0], controls.target) &&
        captions_fit(controls.labels[1], controls.retune) &&
        captions_fit(controls.labels[2], controls.amount, controls.second_page_hint);
}

constexpr bool valid_extra_controls(const ExtraControls& extra, const Controls& controls, u32 row = 0) {
    return row == extra_rows || (extra.choices[row].count && extra.choices[row].count <= extra_capacity[row] &&
        captions_fit(extra.labels[row], extra.choices[row],
                     row + 1 == extra_rows ? controls.first_page_hint : nullptr) &&
        valid_extra_controls(extra, controls, row + 1));
}

struct ExtraValues { u32 values[extra_rows]; };

}}
