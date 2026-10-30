// Selected-pad values and labels used by shared screen/control services.
#include "drum_values.hpp"

bool sdk_drum_values(opxy::u32 track, opxy::u8* output) {
    using namespace opxy::platform;
    if (track >= kTracks || !output) return false;
    const DrumMachine* machine = sdk_drum_machine(track_engine(track));
    if (!machine) return false;
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d6c98)(track);
    return drum_region_values(*machine, track, key, pad_region(track, key), output);
}

const char* sdk_drum_title(opxy::u32 engine, opxy::u32 slot) {
    using namespace opxy;
    using namespace opxy::platform;
    const DrumMachine* machine = sdk_drum_machine(engine);
    const u32 track = byte(kSelectedTrack);
    u8 controls[kDrumControlCount];
    if (!machine || slot >= kDrumControlCount || track >= kTracks || track_engine(track) != engine ||
        !sdk_drum_values(track, controls)) return "";
    return machine->label(controls[0], slot);
}
