// GUI role. 8c0b147a calls the stock per-key editor with the selected/held key,
// control index and encoder event. Keep publication on stock's existing route.
#include "drum_values.hpp"
#include "synth_controls.hpp"
#include "drumline.hpp"

opxy::u32 sdk_drum_edit(opxy::u32 key, opxy::u32 control, const opxy::u32* event, opxy::u32 fine) {
    using namespace opxy;
    using namespace opxy::platform;
    const u32 track = byte(kSelectedTrack);
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    // Stock Drumline's page 2, with a drumline_page extension (drumline.hpp).
    if (!machine && drumline_track(track) && drumline_service()->edit(track, key, control, event)) return 0;
    if (!machine) return stock<u32(u32, u32, const u32*, u32)>(0x8d4d5f60)(key, control, event, fine);
    if (!event || control >= kDrumControlCount) return 0;
    void* region = pad_region(track, key);
    u8 model, controls[kDrumControlCount];
    if (!drum_region_values(*machine, track, key, region, &model, controls)) return 0;
    // Encoder event word one is its signed delta. Bound before addition to
    // avoid overflow and preserve exact integer steps even at high velocity.
    i32 delta = static_cast<i32>(event[1]);
    if (!delta) return 0;
    if (delta > 127) delta = 127;
    if (delta < -127) delta = -127;
    unsigned char* record = static_cast<unsigned char*>(region);
    u8 mark = drum_mark(record[24]);
    i32* pad_word = static_cast<i32*>(region) + kDrumPadWord / 4;
    // A padMix kit's first edit of a key it has not set up sets the key up
    // (drum_storage.hpp): neutral pad fields, 0 dB and the mark. Until then the
    // key played at 0 dB with every field neutral, whatever stock had left in
    // the pad word and the level byte, so setting it up changes nothing heard.
    const bool setup = machine->pad_mix && !drum_ready(record);
    i32 pad = setup ? static_cast<i32>(kDrumPadReady) : *pad_word;
    i32 pan = drum_pan(record), level = setup ? 0 : drum_level(record);
    // Shift's encoders (stock's indices 4..7) choose what DrumMachine::shift
    // names for their position and the row the encoders are on.
    u8 shown[page_controls];
    sdk_synth_shown(track, shown);
    const DrumMachine::Shift choice = control >= page_controls
        ? machine->shift(control - page_controls, shown[0] >= page_controls) : DrumMachine::kShiftNone;
    if (choice == DrumMachine::kShiftPan || choice == DrumMachine::kShiftLevel) {
        // Stock's own pad fields, edited as stock's editor does (8d4d60b0,
        // 8d4d60d8); level as a cut.
        const bool panning = choice == DrumMachine::kShiftPan;
        i32& value = panning ? pan : level;
        const i32 now = value;
        i32 step = panning ? static_cast<i32>(event[2]) + 1 : 1;
        if (step < 1) step = 1;
        if (step > 200) step = 200;
        i32 next = now + step * delta;
        const i32 low = panning ? -kDrumPanLimit : kDrumLevelFloor, high = panning ? kDrumPanLimit : kDrumLevelCeiling;
        if (next < low) next = low;
        if (next > high) next = high;
        if (next == now) return 0;
        value = next;
    } else if (choice == DrumMachine::kShiftGroup) {
        // A group keeps the model's place in it, so BD stays BD across groups.
        const u32 group = machine->group(model);
        i32 next = static_cast<i32>(group) + delta;
        if (next < 0) next = 0;
        if (next >= static_cast<i32>(machine->group_count)) next = static_cast<i32>(machine->group_count) - 1;
        if (next == static_cast<i32>(group)) return 0;
        const u32 place = model - machine->group_first(group), size = machine->group_sizes[next];
        model = static_cast<u8>(machine->group_first(static_cast<u32>(next)) + (place < size ? place : size - 1));
    } else if (choice == DrumMachine::kShiftModel) {
        // The model, as on stock Drumline (8d4d6028 steps its instrument
        // type), within its group if the kit chooses groups on Shift (a padMix
        // kit scrolls them all). Its controls stay.
        const bool within = machine->group_count && !machine->pad_mix;
        const u32 first = within ? machine->group_first(machine->group(model)) : 0;
        const i32 last = static_cast<i32>(within ? first + machine->group_sizes[machine->group(model)]
                                                 : machine->model_count) - 1;
        i32 next = static_cast<i32>(model) + delta;
        if (next < static_cast<i32>(first)) next = static_cast<i32>(first);
        if (next > last) next = last;
        if (next == model) return 0;
        model = static_cast<u8>(next);
    } else if (choice >= DrumMachine::kShiftPad) {
        // One of the key's pad fields: one step a detent within its bits.
        u32 low, bits;
        bool sign;
        if (!machine->pad_field(choice - DrumMachine::kShiftPad, low, bits, sign)) return 0;
        const i32 now = drum_field_get(pad, low, bits, sign);
        i32 next = now + delta;
        if (next < drum_field_min(bits, sign)) next = drum_field_min(bits, sign);
        if (next > drum_field_max(bits, sign)) next = drum_field_max(bits, sign);
        if (next == now) return 0;
        pad = drum_field_set(pad, low, bits, next);
    } else if (choice == DrumMachine::kShiftChoke) {
        const i32 now = static_cast<i32>(drum_choke(record[24]));
        i32 next = now + delta;
        if (next < 0) next = 0;
        if (next >= static_cast<i32>(kDrumChokeChoices)) next = static_cast<i32>(kDrumChokeChoices) - 1;
        if (next == now) return 0;
        mark = drum_mark_choke(static_cast<u32>(next));
    } else if (control >= page_controls) {
        // A Shift encoder with nothing to choose turns nothing.
        return 0;
    } else {
        // The encoders' discrete steps are already one unit a detent.
        const u32 slot = shown[control % page_controls];
        if (slot >= kDrumControlCount || !machine->used(model, slot)) return 0;
        i32 next = static_cast<i32>(controls[slot]) + delta;
        if (next < 0) next = 0;
        if (next > 127) next = 127;
        if (next == controls[slot]) return 0;
        controls[slot] = static_cast<u8>(next);
    }
    if (!drum_pack(model, controls, static_cast<i32*>(region) + 1, drum_codec(machine->model_count, machine->count))) return 0;
    record[24] = mark;
    *pad_word = pad;
    // Pan and level only when they are the edit, or the level as the key is
    // set up: drum_level reads a stored boost as 0 dB, so writing it back on
    // any other edit would replace the boost stock saved.
    if (choice == DrumMachine::kShiftPan)
        record[kDrumPan] = static_cast<unsigned char>(static_cast<signed char>(pan));
    if (machine->pad_mix && (choice == DrumMachine::kShiftLevel || setup))
        record[kDrumLevel] = static_cast<unsigned char>(static_cast<signed char>(level));
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return 0;
}

// 8d4dda96, 8d4ddb84 and 8d4dde44: the three calls of stock's preset import
// 8d4e35a8(preset, flag), which writes the pads of the track at the preset's
// +4 (8d4e22d4) in that track's current pattern (8d587708): the 24 entries at
// +0x394c, 0x80 apart, each region's params, model byte, `random` (as
// max(random << 16, 0), 8d4e3558; 0 for a region without one,
// drum_storage.hpp), hikey, and level and pan. Once it returns,
// for a padMix kit (the pattern's machine at +0x18), every key the preset marked
// is set up as loaded (drum_storage.hpp drum_setup_loaded), so it plays the
// level, pad controls and sends the preset was saved with. Projects load
// through deserialize, never here, so their keys keep reading as saved. Here,
// in a unit of the drums service, which alone patches those calls.
opxy::u32 sdk_drum_preset_load(opxy::u32 preset, opxy::u32 flag) {
    using namespace opxy::platform;
    const opxy::u32 result = stock<opxy::u32(opxy::u32, opxy::u32)>(0x8d4e35a8)(preset, flag);
    const opxy::u32 track = preset ? *at<const volatile opxy::u32>(static_cast<Address>(preset) + 4) : kTracks;
    if (track >= kTracks) return result;
    const Address pattern = stock<Address(opxy::u32)>(0x8d587708)(track);
    const DrumMachine* machine = pattern ? sdk_drum_machine(*at<const volatile opxy::u32>(pattern + kDrumPatternMachine)) : nullptr;
    // Not a kit: a synth's Shift row (SDK 1.13) comes back from the first pad record.
    if (!machine) sdk_synth_shift_import(track);
    if (!machine || !machine->pad_mix) return result;
    for (opxy::u32 k = 0; k < kDrumPatternPads; ++k)
        drum_setup_loaded(at<unsigned char>(pattern + kDrumPatternPadsAt + k * kDrumPatternPadBytes));
    return result;
}

// 8c0b14b4 and 8c0b167a: the instrument controller's turn handler (8c0b12e4)
// asks for the track's last played key (8d4d6cb0) when it holds no key for
// editing (-1 at 0x8400dfc8, stock.hpp): at 8c0b1678 to latch it there, then
// at 8c0b14b2 for the per-key editor's key. On an owned kit's track the latch
// keeps -1 and the editor gets the selected key, so a turn edits the key the
// screen shows, as it moves, never one the sequencer just played; a pad held
// or Shift-selected stays stock's. Any other track keeps stock's answer. Here,
// in a unit of the drums service, which alone patches those calls.
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

// 8c0b5314: the track menu's scramble (hold the track key, 8c0b52fc) asks the
// selected track's engine (8d58781c) and takes Drumline's branch for 4
// (8c0b5430), its synth branch for anything else. An owned kit answers as
// Drumline, as its editor's checks do, so it takes the drum branch.
opxy::u32 sdk_scramble_engine(opxy::u32 track) {
    return sdk_drum_alias(opxy::platform::stock<opxy::u32(opxy::u32)>(0x8d58781c)(track));
}

// 8c0b5430: the drum branch's one call. Stock's (8d4d622c) re-rolls each of the
// 24 pads' instrument type and settings, keeping its choke group, publishes
// each pad (8d4d5f18) and then the change (8d585ee8). On an owned kit each key
// gets a model and a value for every control that model uses, within the
// kit's scramble range, packed as an edit packs them. Its choke group, pan,
// level and pad fields stay; a padMix key the kit had not set up is set up as
// its first edit would be (sdk_drum_edit), so it sounds as before apart from
// the scramble. Other tracks keep stock's.
void sdk_drum_scramble() {
    using namespace opxy;
    using namespace opxy::platform;
    const u32 track = byte(kSelectedTrack);
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    if (!machine) return stock<void()>(0x8d4d622c)();
    const DrumCodec codec = drum_codec(machine->model_count, machine->count);
    if (!codec.fits() || !machine->model_count) return;
    // Stock's own generator seeds it, so each scramble differs.
    u32 seed = static_cast<u32>(stock<i32(u32, u32)>(0x8c0b5230)(0, 0)) | 1u;
    const auto next = [&seed](u32 span) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return ((seed >> 16) * span) >> 16;   // 0 .. span - 1, without a divide
    };
    for (u32 key = 53; key < 77; ++key) {
        void* region = pad_region(track, key);
        u8 model, controls[kDrumControlCount];
        if (!drum_region_values(*machine, track, key, region, &model, controls)) continue;
        model = static_cast<u8>(next(machine->model_count));
        for (u32 slot = 0; slot < kDrumControlCount; ++slot) {
            if (!machine->used(model, slot)) continue;
            u8 low = 0, high = 127;
            if (machine->scramble_range) machine->scramble_range(model, slot, &low, &high);
            if (high > 127) high = 127;
            if (low > high) low = high;
            controls[slot] = static_cast<u8>(low + next(high - low + 1u));
        }
        unsigned char* record = static_cast<unsigned char*>(region);
        const bool setup = machine->pad_mix && !drum_ready(record);
        if (!drum_pack(model, controls, static_cast<i32*>(region) + 1, codec)) continue;
        record[24] = drum_mark(record[24]);
        if (setup) {
            static_cast<i32*>(region)[kDrumPadWord / 4] = static_cast<i32>(kDrumPadReady);
            record[kDrumLevel] = 0;
        }
        stock<void(u32)>(0x8d4d5f18)(key);
    }
    stock<void()>(0x8d585ee8)();
}
