// Selected-pad values and labels used by shared screen/control services.
#include "drum_values.hpp"

bool sdk_drum_values(opxy::u32 track, opxy::u8* model, opxy::u8* controls) {
    using namespace opxy::platform;
    if (track >= kTracks || !model || !controls) return false;
    const DrumMachine* machine = sdk_drum_machine(track_engine(track));
    if (!machine) return false;
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d7650)(track);
    return drum_region_values(*machine, track, key, pad_region(track, key), model, controls);
}

const char* sdk_drum_choke(opxy::u32 track) {
    using namespace opxy::platform;
    static const char* const names[kDrumChokeChoices] = {"NONE", "1", "2", "3"};
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return "";
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d7650)(track);
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, key));
    return record ? names[drum_choke(record[24])] : "";
}

const char* sdk_drum_title(opxy::u32 engine, opxy::u32 slot) {
    using namespace opxy;
    using namespace opxy::platform;
    const DrumMachine* machine = sdk_drum_machine(engine);
    const u32 track = byte(kSelectedTrack);
    u8 model, controls[kDrumControlCount];
    if (!machine || slot >= kDrumControlCount || track >= kTracks || track_engine(track) != engine ||
        !sdk_drum_values(track, &model, controls)) return "";
    return machine->label(model, slot);
}
