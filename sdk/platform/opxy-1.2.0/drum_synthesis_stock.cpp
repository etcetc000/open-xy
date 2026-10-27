// GUI role. 8c0b1fda calls the stock per-key editor with the selected/held key,
// control index and encoder event. Keep publication on stock's existing route.
#include "drum_values.hpp"
#include "synth_controls.hpp"

opxy::u32 sdk_drum_edit(opxy::u32 key, opxy::u32 control, const opxy::u32* event, opxy::u32 fine) {
    using namespace opxy;
    using namespace opxy::platform;
    const u32 track = byte(kSelectedTrack);
    const DrumMachine* machine = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    if (!machine) return stock<u32(u32, u32, const u32*, u32)>(0x8d4d6908)(key, control, event, fine);
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
    if (control == 5) {
        // Shift and the second encoder choose the key's choke group.
        const i32 choice = static_cast<i32>(drum_choke(record[24]));
        i32 next = choice + delta;
        if (next < 0) next = 0;
        if (next >= static_cast<i32>(kDrumChokeChoices)) next = static_cast<i32>(kDrumChokeChoices) - 1;
        if (next == choice) return 0;
        mark = drum_mark_choke(static_cast<u32>(next));
    } else if (control == 4) {
        // Shift and the first encoder choose the key's model, as on stock
        // Drumline (8d4d69d0 steps its instrument type). Its controls stay.
        i32 next = static_cast<i32>(model) + delta;
        if (next < 0) next = 0;
        if (next >= static_cast<i32>(machine->model_count)) next = static_cast<i32>(machine->model_count) - 1;
        if (next == model) return 0;
        model = static_cast<u8>(next);
    } else if (control >= page_controls) {
        // Stock maps Shift's third and fourth encoders to indices 6 and 7:
        // their cells name the choke group, so they turn nothing.
        return 0;
    } else {
        // The encoders' discrete steps are already one unit a detent.
        u8 shown[page_controls];
        sdk_synth_shown(track, shown);
        const u32 slot = shown[control % page_controls];
        if (slot >= kDrumControlCount || !machine->used(model, slot)) return 0;
        i32 next = static_cast<i32>(controls[slot]) + delta;
        if (next < 0) next = 0;
        if (next > 127) next = 127;
        if (next == controls[slot]) return 0;
        controls[slot] = static_cast<u8>(next);
    }
    if (!drum_pack(model, controls, static_cast<i32*>(region) + 1)) return 0;
    record[24] = mark;
    stock<void()>(0x8d584584)();
    stock<void(u32)>(0x8d4d68c0)(key);
    return 0;
}
