// A synth's Shift row (SDK 1.13): up to eight more controls, entry i under the
// engine page's page i / 4 at position i % 4, which Shift and that position's
// encoder turn. They live in two words of the instrument track, 36 (entries 0..3)
// and 37 (4..7), which no stock page, preset key or engine reads on a synth track,
// so stock stores, saves and loads them with every pattern as any track word
// (docs/abi-1.2.1.md#a-synths-shift-row). Each word holds four 7-bit fields, the
// first entry lowest, and a mark: a word without it (a new project, a preset
// load, a project saved before the machine) plays every entry's default.
//
// A continuous entry's field is 0..100 and plays field / 100 of full scale; an
// enum entry's is its choice, played as the middle of the choice's bucket, as
// stock stores a stepped choice. The GUI (synth_controls.cpp) writes the words;
// the voice adapter (voice.cpp.in) reads them from the track record stock
// renders the voice with and passes them as parameters 8..15.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace synth_shift {
const u32 kFirstWord = 36;     // entries 0..3; kFirstWord + 1 holds 4..7
const u32 kEntries = 8, kPerWord = 4, kBits = 7, kFieldMask = 0x7f;
const i32 kMark = 0x40000000;
const u32 kAmountTop = 100;    // a continuous entry's field runs 0..100
// SynthShift::choices: an enum's choice count (2..128), else one of these.
const u8 kContinuous = 0, kAbsent = 1;

// The track word holding an entry, and the entry's field in it.
inline u32 word_of(u32 entry) { return kFirstWord + entry / kPerWord; }
inline u32 field(i32 word, u32 entry, const u8* defaults) {
    return word & kMark ? (static_cast<u32>(word) >> (kBits * (entry % kPerWord))) & kFieldMask : defaults[entry];
}
// An entry's highest field: its last choice, 100, or 0 when absent.
inline u32 top(u8 choices) { return choices >= 2 ? choices - 1u : choices == kAbsent ? 0u : kAmountTop; }
inline u32 bounded(u32 value, u8 choices) { return value > top(choices) ? top(choices) : value; }
// The word with entry's field set to value and the word's other entries kept, as
// their defaults while the word has no mark.
inline i32 with(i32 word, u32 entry, u32 value, const u8* defaults) {
    const u32 first = entry / kPerWord * kPerWord;
    u32 out = static_cast<u32>(kMark);
    for (u32 i = 0; i < kPerWord; ++i)
        out |= ((first + i == entry ? value : field(word, first + i, defaults)) & kFieldMask) << (kBits * i);
    return static_cast<i32>(out);
}
// The Q31 word an entry plays, as render and the display receive it: a continuous
// field as round(field * 0x7fffffff / 100), without a division (n * 5243 >> 19 is
// n / 100 for every n this takes); a choice as the middle of its bucket (bucket =
// 0x7fffffff / choices, which the voice adapter keeps as a constant).
inline i32 amount(u32 f) {
    return f >= kAmountTop ? 0x7fffffff : static_cast<i32>(f * 21474836u + (((f * 47u + 50u) * 5243u) >> 19));
}
inline i32 choice(u32 c, u32 bucket) { return static_cast<i32>(c * bucket + bucket / 2); }
inline i32 q31(u32 f, u8 choices) {
    return choices >= 2 ? choice(bounded(f, choices), 0x7fffffffu / choices) : choices == kAbsent ? 0 : amount(bounded(f, choices));
}
}}}
