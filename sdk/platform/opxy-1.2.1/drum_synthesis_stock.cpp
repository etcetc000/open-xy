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
    // Stock Drumline's second Shift row, with the build option drumlineRow2 (drumline.hpp).
    if (!machine && drumline_track(track) && sdk_drumline->edit(track, key, control, event)) return 0;
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
    // A padMix kit clears the pad fields when it first marks a pad: stock may
    // have left its own value in that word.
    i32 pad = machine->pad_mix && !drum_marked(record[24]) ? *pad_word & ~static_cast<i32>(kDrumPadMask) : *pad_word;
    // Shift's encoders (stock's indices 4..7) choose what DrumMachine::shift
    // names for their position and the row the encoders are on.
    u8 shown[page_controls];
    sdk_synth_shown(track, shown);
    const DrumMachine::Shift choice = control >= page_controls
        ? machine->shift(control - page_controls, shown[0] >= page_controls) : DrumMachine::kShiftNone;
    if (choice == DrumMachine::kShiftPan || choice == DrumMachine::kShiftLevel) {
        // Stock's own pad fields, edited as stock's editor does (8d4d60b0,
        // 8d4d60d8); level as a cut. The four words and the mark stay.
        const bool pan = choice == DrumMachine::kShiftPan;
        const i32 now = pan ? drum_pan(record) : drum_level(record);
        i32 step = pan ? static_cast<i32>(event[2]) + 1 : 1;
        if (step < 1) step = 1;
        if (step > 200) step = 200;
        i32 next = now + step * delta;
        const i32 low = pan ? -kDrumPanLimit : kDrumLevelFloor, high = pan ? kDrumPanLimit : kDrumLevelCeiling;
        if (next < low) next = low;
        if (next > high) next = high;
        if (next == now) return 0;
        record[pan ? kDrumPan : kDrumLevel] = static_cast<unsigned char>(static_cast<signed char>(next));
        stock<void()>(0x8d585ee8)();
        stock<void(u32)>(0x8d4d5f18)(key);
        return 0;
    }
    if (choice == DrumMachine::kShiftGroup) {
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
        const i32 now = drum_marked(record[24]) ? drum_field_get(pad, low, bits, sign) : 0;
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
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return 0;
}
