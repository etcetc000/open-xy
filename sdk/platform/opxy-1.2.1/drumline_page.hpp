// A drumline_page extension's page as the SDK reads it: the table
// tooling/extensions.py generates from its extension.json into the extension's
// own object, behind sdk_drumline_page_extension (drumline.hpp).
#pragma once
#include <opxy/drumline_page.hpp>

namespace opxy { namespace platform {
// What a parameter is for: the extension's own (its processor reads it), or a
// key's FX I or FX II send, which the SDK mixes (drum_send.hpp).
enum DrumlineRole { kDrumlineOwn = 0, kDrumlineSend1 = 1, kDrumlineSend2 = 2 };

struct DrumlinePageParameter {
    const char* label;           // the title, 1..7 characters
    const char* const* choices;  // `count` captions, 1..7 characters each
    u32 count;                   // 2..32 choices
    u32 neutral;                 // the default choice, which stores and carries as 0
    u32 role;                    // DrumlineRole; a send has 8 choices, TRK first, neutral 0
    u32 shift, bits;             // the code's place among the page's 16 code bits
};

struct DrumlinePage {
    u32 tag;                                // the owner tag, 1..0xffff
    const drumline::Processor* processor;
    const DrumlinePageParameter* parameters;
    u32 count;                              // 1..4
};
}} // namespace opxy::platform
