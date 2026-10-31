// Selected-pad values and labels used by shared screen/control services.
#include "drum_values.hpp"
#include "drum_lock_store.hpp"
#include <opxy/draw.hpp>
extern "C" void sdk_drum_trigger(opxy::u32, opxy::draw::Input*) __asm__("sdk_drum_trigger");

void sdk_drum_trigger(opxy::u32 track, opxy::draw::Input* input) {
    using namespace opxy;
    using namespace opxy::platform;
    input->trigger_key = input->trigger_model = 0;
    for (u32 i = 0; i < 8; ++i) input->trigger_controls[i] = 0;
    if (track >= kTracks) return;
    // Stock's last played pitch accompanies the same start counter for live
    // notes (8d595838) and sequenced notes (8d592092). The held-key map only
    // describes live gates and cannot identify sequenced drum strikes.
    const u32 note = stock<u32(u32)>(0x8d4d6cb0)(track);
    if (note >= 128) return;
    const DrumMachine* machine = sdk_drum_machine(track_engine(track));
    const void* region = machine ? pad_region(track,note) : nullptr;
    u8 model, controls[8], live[8];
    if (!region || !drum_region_values(*machine,track,note,region,&model,controls)) return;
    const u32 key = drum_canonical_key(track,note,region);
    if (key < 53 || key >= 77) return;
    const u32* parameters = stock<const u32*(u32)>(0x8d4d72a8)(track);
    const u32 begin = parameters ? parameters[1] : 0, end = parameters ? parameters[2] : 0;
    const i32* offsets = begin && !(begin & 3) && end >= begin && end-begin >= 32
        ? at<const i32>(begin) : nullptr;
    drum_offset(controls,offsets,live);
    input->trigger_key = key;
    input->trigger_model = model;
    for (u32 i = 0; i < 8; ++i)
        input->trigger_controls[i] = live[i] == 127 ? 0x7fffffff : static_cast<i32>(u32(live[i]) << 24);
}

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
