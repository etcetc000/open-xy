#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace draw {
const u32 overview_bins = 96;

// The sample a sample engine's own screen shows: the selected track's, as the
// engine plays it. low[i] and high[i] are the smallest and largest 16-bit values
// of any channel in bin i of 96 equal parts of the whole sample; begin and end are
// its outer markers, in frames, and reverse is 1 when it plays backwards.
// assigned is 0 while the track holds no sample (a screen can say how to load
// one); loaded is 0 until the overview is ready, as the sample loads and while
// the file worker reads it in the background; frames, rate, channels and the bins
// are then 0 too. generation changes whenever a different overview is published,
// including when the same file is replaced or reloaded, and is never 0 once
// loaded: redraw a waveform when it moves. Read-only and display-only: audio
// callbacks never call it.
struct SampleOverview {
    u32 generation;
    u32 assigned;
    u32 loaded;
    u32 frames;
    u32 rate;
    u32 channels;
    u32 begin, end;
    u32 reverse;
    i16 low[overview_bins], high[overview_bins];
};
}}

// The overview for the screen being drawn; never null. Only a sample engine's
// own screen (display.md) receives a real one; the browser preview supplies
// whatever its host writes (opxy_display_sample).
extern "C" const opxy::draw::SampleOverview* opxy_sample_overview();
