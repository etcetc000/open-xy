// Selected-pad values and labels used by shared screen/control services.
#include "drum_values.hpp"

bool sdk_drum_values(opxy::u32 track, opxy::u8* model, opxy::u8* controls) {
    using namespace opxy::platform;
    if (track >= kTracks || !model || !controls) return false;
    const DrumMachine* machine = sdk_drum_machine(track_engine(track));
    if (!machine) return false;
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d6c98)(track);
    return drum_region_values(*machine, track, key, pad_region(track, key), model, controls);
}

opxy::u32 sdk_drum_selected_pad(opxy::u32 track) {
    using namespace opxy::platform;
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return 0;
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d6c98)(track);
    const void* region = pad_region(track, key);
    const opxy::u32 pad = region ? drum_canonical_key(track, key, region) : 0;
    return pad >= 53 && pad < 77 ? pad : 0;
}

opxy::u32 sdk_drum_pad(opxy::u32 track, opxy::u32 note) {
    using namespace opxy::platform;
    if (track >= kTracks || note >= 128) return 0;
    const opxy::u32 pad = stock<opxy::u32(opxy::u32, opxy::u32)>(0x8d589210)(track, note);
    return pad >= 53 && pad < 77 ? pad : 0;
}

const char* sdk_drum_choke(opxy::u32 track) {
    using namespace opxy::platform;
    static const char* const names[kDrumChokeChoices] = {"NONE", "1", "2", "3"};
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return "";
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d6c98)(track);
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, key));
    return record ? names[drum_choke(record[24])] : "";
}

opxy::u32 sdk_drum_choke_choice(opxy::u32 track) {
    using namespace opxy::platform;
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return 0;
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d6c98)(track);
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, key));
    return record ? drum_choke(record[24]) : 0;
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

// 8d4dda96, 8d4ddb84 and 8d4dde44: the three calls of stock's preset import
// 8d4e35a8(preset, flag), which writes the pads of the track at the preset's
// +4 (8d4e22d4) in that track's current pattern (8d587708): the 24 entries at
// +0x394c, 0x80 apart, each region's params, model byte, `random` (as
// max(random << 16, 0), 8d4e3558), hikey, and level and pan. Once it returns,
// for a padMix kit (the pattern's machine at +0x18), every key the preset marked
// is set up as loaded (drum_storage.hpp drum_setup_loaded), so it plays the
// level, pad controls and sends the preset was saved with. Projects load
// through deserialize, never here, so their keys keep reading as saved.
opxy::u32 sdk_drum_preset_load(opxy::u32 preset, opxy::u32 flag) {
    using namespace opxy::platform;
    const opxy::u32 result = stock<opxy::u32(opxy::u32, opxy::u32)>(0x8d4e35a8)(preset, flag);
    const opxy::u32 track = preset ? *at<const volatile opxy::u32>(static_cast<Address>(preset) + 4) : kTracks;
    if (track >= kTracks) return result;
    const Address pattern = stock<Address(opxy::u32)>(0x8d587708)(track);
    const DrumMachine* machine = pattern ? sdk_drum_machine(*at<const volatile opxy::u32>(pattern + kDrumPatternMachine)) : nullptr;
    if (!machine || !machine->pad_mix) return result;
    for (opxy::u32 k = 0; k < kDrumPatternPads; ++k)
        drum_setup_loaded(at<unsigned char>(pattern + kDrumPatternPadsAt + k * kDrumPatternPadBytes));
    return result;
}

// The selected key's stock pan, which stock plays on every key, and its level
// as a padMix kit plays it: 0 dB on a key the kit has not set up.
static const unsigned char* selected_record(opxy::u32 track) {
    using namespace opxy::platform;
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return nullptr;
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d6c98)(track);
    return static_cast<const unsigned char*>(pad_region(track, key));
}
opxy::i32 sdk_drum_pan(opxy::u32 track) { return opxy::platform::drum_pan(selected_record(track)); }
opxy::i32 sdk_drum_level(opxy::u32 track) {
    const unsigned char* record = selected_record(track);
    return opxy::platform::drum_ready(record) ? opxy::platform::drum_level(record) : 0;
}
void sdk_drum_pad_values(opxy::u32 track, opxy::i32* values) {
    using namespace opxy::platform;
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    const unsigned char* record = machine ? selected_record(track) : nullptr;
    const bool ready = drum_ready(record);
    for (opxy::u32 i = 0; i < kDrumPadFields; ++i) {
        opxy::u32 low, bits;
        bool sign;
        values[i] = ready && machine->pad_field(i, low, bits, sign)
            ? drum_field_get(reinterpret_cast<const opxy::i32*>(record)[kDrumPadWord / 4], low, bits, sign) : 0;
    }
}
