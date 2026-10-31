#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace draw {
const u32 overview_bins = 64;

// The sample a sample engine's own screen shows: the selected track's, as the
// engine plays it. peaks[i] is the largest magnitude (0..255) in bin i of 64
// equal parts of the whole sample; begin and end are its outer markers, in
// frames. assigned is 0 while the track holds no sample (a screen can say how
// to load one); loaded is 0 until every peak has been read, as the sample
// loads and for a few frames after. Read-only and display-only: audio
// callbacks never call it.
struct SampleOverview {
    u32 assigned;
    u32 loaded;
    u32 frames;
    u32 rate;
    u32 begin, end;
    u8 peaks[overview_bins];
};
}}

// The overview for the screen being drawn; never null. Only a sample engine's
// own screen (display.md) receives a real one; the browser preview supplies
// whatever its host writes (opxy_display_sample).
extern "C" const opxy::draw::SampleOverview* opxy_sample_overview();
