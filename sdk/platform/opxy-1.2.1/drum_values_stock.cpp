// Selected-pad values and labels used by shared screen/control services.
#include "drum_values.hpp"
#include "drum_locks.hpp"

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

// The selected key's stock pan, and its level as a padMix kit plays it.
static const unsigned char* selected_record(opxy::u32 track) {
    using namespace opxy::platform;
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return nullptr;
    const opxy::u32 key = stock<opxy::u32(opxy::u32)>(0x8d4d6c98)(track);
    return static_cast<const unsigned char*>(pad_region(track, key));
}
// While a step is held, the offsets a padMix kit's track locks on it
// (drum_locks.hpp), as stock's editor shows a held step's value (8d5ac93c: its
// lock, else the stored word); otherwise none.
static void held_offsets(opxy::u32 track, opxy::i32* offsets) {
    using namespace opxy::platform;
    for (opxy::u32 i = 0; i < kDrumLocks; ++i) offsets[i] = 0;
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    if (!machine || !machine->pad_mix || !stock<opxy::u32()>(0x8d5b7e34)()) return;
    opxy::i32 words[kDrumLocks];
    for (opxy::u32 i = 0; i < kDrumLocks; ++i)
        words[i] = stock<opxy::i32(opxy::u32, opxy::u32, opxy::u32)>(0x8d5ac93c)(0x86bd027c, track, kDrumLockWord + i);
    drum_lock_offsets(*machine, words, offsets);
}
// With a step held, each as that step plays it.
opxy::i32 sdk_drum_pan(opxy::u32 track) {
    using namespace opxy::platform;
    opxy::i32 offsets[kDrumLocks];
    held_offsets(track, offsets);
    const opxy::i32 pan = drum_pan(selected_record(track));
    return offsets[0] ? drum_lock_clamp(pan + offsets[0], -kDrumPanLimit, kDrumPanLimit) : pan;
}
opxy::i32 sdk_drum_level(opxy::u32 track) {
    using namespace opxy::platform;
    opxy::i32 offsets[kDrumLocks];
    held_offsets(track, offsets);
    return drum_lock_clamp(drum_level(selected_record(track)) + offsets[1], kDrumLevelFloor, kDrumLevelCeiling);
}
void sdk_drum_pad_values(opxy::u32 track, opxy::i32* values) {
    using namespace opxy::platform;
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    const unsigned char* record = machine ? selected_record(track) : nullptr;
    const bool marked = record && drum_marked(record[24]);
    opxy::i32 offsets[kDrumLocks];
    held_offsets(track, offsets);
    for (opxy::u32 i = 0; i < kDrumPadFields; ++i) {
        opxy::u32 low, bits;
        bool sign;
        values[i] = 0;
        if (!record || !machine->pad_field(i, low, bits, sign)) continue;
        if (marked) values[i] = drum_field_get(reinterpret_cast<const opxy::i32*>(record)[kDrumPadWord / 4], low, bits, sign);
        if (i < kDrumKitPadFields && offsets[2 + i])
            values[i] = drum_lock_clamp(values[i] + offsets[2 + i], drum_field_min(bits, sign), drum_field_max(bits, sign));
    }
}
