#pragma once
#include <opxy/types.hpp>
namespace opxy {
// Grain markers are actual positions of the latest started matching voice.
// A single-sample engine can also show its assigned waveform before any note.
struct SampleMarker { u32 frame, phase_q16, flags; };
struct SampleVisual { u32 count; SampleMarker markers[4]; };
enum SampleDisplayFlags : u32 { sample_waveform_approximate = 1, sample_voice_live = 2, sample_waveform_ready = 4 };
struct SampleDisplaySnapshot {
    u32 track, generation, frames, flags;
    i16 minimum[128], maximum[128];
    u32 marker_count;
    SampleMarker markers[4];
};
// GUI only. Copies a source-bound waveform and current real-voice markers.
// Single-sample engines retain the assigned waveform at rest; stale markers clear.
// False
// means pending/unavailable; callers must not draw an old track's snapshot.
// The waveform samples the source conservatively; it is not an exact peak scan.
bool sample_display_snapshot(u32 track, SampleDisplaySnapshot& out);
}
