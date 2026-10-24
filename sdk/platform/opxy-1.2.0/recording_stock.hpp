#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
// Called around stock's accepted live insertion, never the audio dispatcher.
u32 recording_input_count(u32 state, u32 track);
void recording_input_done(u32 previous_count, u32 track, u32 pitch);
void recording_inserted(u32 track, u32 slot);
void recording_sync();
}}
