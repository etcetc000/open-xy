#pragma once
#include "types.hpp"

namespace opxy {
// GUI-only sample context. A copied overview, never PCM or an audio-state
// pointer. Peaks combine both channels and cover the complete loaded sample.
const u32 sample_display_bins = 96;
struct SampleDisplay {
    u32 generation;        // Changes when a different overview is published.
    u32 frames, rate, channels;
    u32 begin, end;         // Current trim in source frames, half-open.
    u32 reverse;
    u32 active_notes;      // Distinct gated MIDI notes on this track, excluding release tails.
    i16 low[sample_display_bins], high[sample_display_bins];
};
}
extern "C" bool opxy_sample_display(opxy::u32 track, opxy::SampleDisplay* output);
namespace opxy {
// Call only from GUI/display code. Always writes output; false means the
// waveform is unavailable/pending. active_notes remains meaningful when false.
// A host may provide this function with recorded or synthetic snapshots.
inline bool sample_display(u32 track, SampleDisplay& output) {
    return opxy_sample_display(track, &output);
}
}
