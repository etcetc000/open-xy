// The track stage runtime (track_stage.hpp), called from the shared track call
// handler (track_call.s) once per block for each instrument track, after stock's
// voice loop. Linked only in a build with a synth that declares a track stage.
#include "track_stage.hpp"
#include "stock.hpp"

namespace stage = opxy::platform::track_stage;
using opxy::i32;
using opxy::u32;

extern "C" {
u32 sdk_track_stage(u32 track, i32* sum, u32 voices) __asm__("sdk_track_stage");
}

namespace {
stage::Track tracks[stage::kTracks];
// The engine each instrument track holds on the audio side (lfo_stock.cpp audio_engine).
const u32 kAudioEngines = 0x11b01d48;
}

// After stock's voice loop rendered `voices` voices of `track` into its sum;
// returns the count the engine continues with.
u32 sdk_track_stage(u32 track, i32* sum, u32 voices) {
    if (track >= stage::kTracks) return voices;
    return stage::block(tracks[track], sdk_track_stages, sdk_track_stage_count, track,
                        opxy::platform::word(kAudioEngines + track * 4), sum, stage::words(track), voices);
}
