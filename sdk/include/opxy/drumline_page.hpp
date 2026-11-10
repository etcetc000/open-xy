// Stock Drumline's second page (docs/drumline-page.md): one extension per build
// declares the page's parameters in its extension.json ("drumline_page") and
// supplies this processor, which runs on each stock Drumline voice.
#pragma once
#include <opxy/audio.hpp>

namespace opxy { namespace drumline {
const u32 frames = block_frames, sample_rate = 44100;
const u32 max_parameters = 4;

// One hit's choices: each declared parameter's enum index, in declaration
// order, as the key held them when the hit started (a step's locks included).
// Entries past the declared parameters are 0.
struct Choices {
    u8 choice[max_parameters];
};

// A voice's own state, zeroed before start(); the SDK keeps one per stock voice.
const u32 voice_words = 8;
struct Voice {
    i32 word[voice_words];
};

// Audio role: no allocation, blocking, I/O or drawing; bounded cost.
// Neither runs for a hit whose parameters other than sends are all at their
// defaults: that voice is stock's, bit for bit.
struct Processor {
    // The hit's first block, before process().
    void (*start)(Voice& voice, const Choices& choices);
    // Each block of the hit: transform the voice's mono output, `frames` Q31
    // samples after stock's level and envelope and before its pan, in place.
    void (*process)(Voice& voice, const Choices& choices, i32* block);
};
}} // namespace opxy::drumline
