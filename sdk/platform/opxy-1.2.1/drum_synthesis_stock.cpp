// GUI role. 8c0b147a calls the stock per-key editor with the selected/held key,
// control index and encoder event. Keep publication on stock's existing route.
#include "drum_values.hpp"
#include "synth_controls.hpp"

opxy::u32 sdk_drum_edit(opxy::u32 key, opxy::u32 control, const opxy::u32* event, opxy::u32 fine) {
    using namespace opxy;
    using namespace opxy::platform;
    const u32 track = byte(kSelectedTrack);
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
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
    // Shift's encoders: a kit with groups chooses the group, the model in it
    // and the choke group on the first three; one without, the model and the
    // choke group on the first two.
    const bool grouped = machine->group_count != 0;
    if (grouped && control == 4) {
        // A group keeps the model's place in it, so BD stays BD across groups.
        const u32 group = machine->group(model);
        i32 next = static_cast<i32>(group) + delta;
        if (next < 0) next = 0;
        if (next >= static_cast<i32>(machine->group_count)) next = static_cast<i32>(machine->group_count) - 1;
        if (next == static_cast<i32>(group)) return 0;
        const u32 place = model - machine->group_first(group), size = machine->group_sizes[next];
        model = static_cast<u8>(machine->group_first(static_cast<u32>(next)) + (place < size ? place : size - 1));
    } else if (grouped && control == 5) {
        const u32 first = machine->group_first(machine->group(model));
        const i32 last = static_cast<i32>(first + machine->group_sizes[machine->group(model)]) - 1;
        i32 next = static_cast<i32>(model) + delta;
        if (next < static_cast<i32>(first)) next = static_cast<i32>(first);
        if (next > last) next = last;
        if (next == model) return 0;
        model = static_cast<u8>(next);
    } else if (control == (grouped ? 6u : 5u)) {
        // Shift and the encoder after the model choose the key's choke group.
        const i32 choice = static_cast<i32>(drum_choke(record[24]));
        i32 next = choice + delta;
        if (next < 0) next = 0;
        if (next >= static_cast<i32>(kDrumChokeChoices)) next = static_cast<i32>(kDrumChokeChoices) - 1;
        if (next == choice) return 0;
        mark = drum_mark_choke(static_cast<u32>(next));
    } else if (control == 4) {
        // Shift and the first encoder choose the key's model, as on stock
        // Drumline (8d4d6028 steps its instrument type). Its controls stay.
        i32 next = static_cast<i32>(model) + delta;
        if (next < 0) next = 0;
        if (next >= static_cast<i32>(machine->model_count)) next = static_cast<i32>(machine->model_count) - 1;
        if (next == model) return 0;
        model = static_cast<u8>(next);
    } else if (control >= page_controls) {
        // Stock maps Shift's encoders to indices 4..7; the rest turn nothing.
        return 0;
    } else {
        // The encoders' discrete steps are already one unit a detent; a slot
        // the kit stores at 64 steps moves one of them.
        u8 shown[page_controls];
        sdk_synth_shown(track, shown);
        const u32 slot = shown[control % page_controls];
        if (slot >= kDrumControlCount || !machine->used(model, slot)) return 0;
        const bool coarse = drum_coarse(machine->model_count, slot);
        i32 next = static_cast<i32>(coarse ? drum_coarse_step(controls[slot]) : controls[slot]) + delta;
        const i32 top = coarse ? 63 : 127;
        if (next < 0) next = 0;
        if (next > top) next = top;
        const u8 value = coarse ? drum_coarse_value(static_cast<u32>(next)) : static_cast<u8>(next);
        if (value == controls[slot]) return 0;
        controls[slot] = value;
    }
    if (!drum_pack(model, controls, static_cast<i32*>(region) + 1, machine->model_count)) return 0;
    record[24] = mark;
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return 0;
}
