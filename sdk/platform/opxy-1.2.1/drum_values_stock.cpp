// Selected-pad values and labels used by shared screen/control services.
#include "drum_values.hpp"
#include "drum_lock_store.hpp"

bool sdk_drum_values(opxy::u32 track, opxy::u8* model, opxy::u8* controls) {
    using namespace opxy::platform;
    if (track >= kTracks || !model || !controls) return false;
    const DrumMachine* machine = sdk_drum_machine(track_engine(track));
    if (!machine) return false;
    const opxy::u32 key = drum_edit_key(track);
    return drum_region_values(*machine, track, key, pad_region(track, key), model, controls);
}

opxy::u32 sdk_drum_selected_pad(opxy::u32 track) {
    using namespace opxy::platform;
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return 0;
    const opxy::u32 key = drum_edit_key(track);
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
    const opxy::u32 key = drum_edit_key(track);
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, key));
    return record ? names[drum_choke(record[24])] : "";
}

opxy::u32 sdk_drum_choke_choice(opxy::u32 track) {
    using namespace opxy::platform;
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return 0;
    const opxy::u32 key = drum_edit_key(track);
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

// 8c0b14b4 and 8c0b167a: the instrument controller's turn handler (8c0b12e4)
// asks for the track's last played key (8d4d6cb0) when it holds no key for
// editing (-1 at 0x8400dfc8, stock.hpp): at 8c0b1678 to latch it there, then
// at 8c0b14b2 for the per-key editor's key. On an owned kit's track the latch
// keeps -1 and the editor gets the selected key, so a turn edits the key the
// screen shows, as it moves, never one the sequencer just played; a pad held
// or Shift-selected stays stock's. Any other track keeps stock's answer.
static bool owned_kit(opxy::u32 track) {
    using namespace opxy::platform;
    return track < kTracks && sdk_drum_machine(track_engine(track));
}
opxy::u32 sdk_drum_unheld_key(opxy::u32 track) {
    using namespace opxy::platform;
    return owned_kit(track) ? drum_selected_key(track) : drum_played_key(track);
}
opxy::u32 sdk_drum_latch_key(opxy::u32 track) {
    using namespace opxy::platform;
    return owned_kit(track) ? 0xffffffffu : drum_played_key(track);
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


// The record of the key the kit's screen shows and its edits change (stock.hpp drum_edit_key).
static const unsigned char* selected_record(opxy::u32 track) {
    using namespace opxy::platform;
    if (track >= kTracks || !sdk_drum_machine(track_engine(track))) return nullptr;
    const opxy::u32 key = drum_edit_key(track);
    return static_cast<const unsigned char*>(pad_region(track, key));
}
// While a step is held, the locks a padMix kit's track has on it
// (drum_lock_store.hpp), as stock's editor shows a held step's value (8d5ac93c:
// its lock, else the stored word); otherwise none.
static void held_locks(opxy::u32 track, opxy::platform::DrumHitLocks& locks) {
    using namespace opxy::platform;
    drum_hit_clear(locks);
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    if (machine && machine->pad_mix && stock<opxy::u32()>(0x8d5b7e34)()) drum_lock_store_held(*machine, track, locks);
}
// The key's stock pan, which stock plays on every key, its level and its pad
// fields as a padMix kit plays them: 0 dB and neutral on a key the kit has not
// set up (drum_storage.hpp). With a step held, each as that step plays it: PAN,
// LEVEL and the pad controls with their offsets, a send with its override.
opxy::i32 sdk_drum_pan(opxy::u32 track) {
    using namespace opxy::platform;
    DrumHitLocks locks;
    held_locks(track, locks);
    return drum_lock_clamp(drum_pan(selected_record(track)) + locks.offset[kHitPan], -kDrumPanLimit, kDrumPanLimit);
}
opxy::i32 sdk_drum_level(opxy::u32 track) {
    using namespace opxy::platform;
    DrumHitLocks locks;
    held_locks(track, locks);
    // A key the kit has not set up plays at 0 dB (drum_storage.hpp).
    const unsigned char* record = selected_record(track);
    const opxy::i32 level = drum_ready(record) ? drum_level(record) : 0;
    return drum_lock_clamp(level + locks.offset[kHitLevel], kDrumLevelFloor, kDrumLevelCeiling);
}
void sdk_drum_pad_values(opxy::u32 track, opxy::i32* values) {
    using namespace opxy::platform;
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    const unsigned char* record = machine ? selected_record(track) : nullptr;
    const bool ready = drum_ready(record);
    DrumHitLocks locks;
    held_locks(track, locks);
    for (opxy::u32 i = 0; i < kDrumPadFields; ++i) {
        opxy::u32 low, bits;
        bool sign;
        values[i] = 0;
        if (!record || !machine->pad_field(i, low, bits, sign)) continue;
        if (ready) values[i] = drum_field_get(reinterpret_cast<const opxy::i32*>(record)[kDrumPadWord / 4], low, bits, sign);
        const opxy::i32 lock = locks.offset[kHitPad1 + i];   // the pad controls, then FX I and FX II
        if (!lock) continue;
        values[i] = i < kDrumKitPadFields
            ? drum_lock_clamp(values[i] + lock, drum_field_min(bits, sign), drum_field_max(bits, sign)) : lock - 1;
    }
}
