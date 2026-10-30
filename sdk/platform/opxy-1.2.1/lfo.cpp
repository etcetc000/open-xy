#include "lfo.hpp"

namespace {
using namespace opxy;
using namespace opxy::platform;
const LfoMachine* machine(u32 engine) {
    for (u32 i = 0; i < sdk_lfo_machine_count; ++i) {
        const LfoMachine& m = sdk_lfo_machines[i];
        if (static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(m.id)) == engine)
            return m.parameters > 4 && m.parameters <= parameter_slots && m.ready() == 1 ? &m : nullptr;
    }
    return nullptr;
}
u32 extended(u32 engine) {
    const LfoMachine* m = machine(engine);
    return m ? m->parameters : 0;
}
u32 target_count(u32 track, u32 index) {
    if (track >= 8 || (index != 27 && index != 31)) return 0;
    const u32 kind = lfo::type(track);
    if (!(((kind == 1 || kind == 3) && index == 27) || (kind == 2 && index == 31))) return 0;
    const u32 count = extended(lfo::gui_engine(track));
    if (!count) return 0;
    const u32 value = lfo::group_value(track);
    const u32 group = kind == 3 ? value >> 29 : ((value >> 8) * 6) >> 24;
    return group == 0 ? count : 0;
}
bool extra_icon(u32 group, u32 slot) {
    if (group != 0 || slot < page_controls || slot >= parameter_slots) return false;
    const u32 track = lfo::selected_track();
    return track < 8 && extended(lfo::gui_engine(track));
}
}

// All four stock display callers use this result to choose eight positions
// instead of dividing the decoded selector by two. Stock's function always
// returns zero; keep that result outside an owned engine's destination group.
u32 sdk_lfo_range(u32 track, u32 group, u32) {
    return track < 8 && group == 0 && extended(lfo::gui_engine(track)) ? 1 : 0;
}

// Stock scrolls a strip containing four knob icons by -spacing * slot.
// The three callers pass their already-decoded group and full destination;
// only the icon scroll follows the assigned encoder. Retained destinations
// beyond the machine's count resolve to its last slot, matching audio.
u32 sdk_lfo_icon_slide(u32 controller, u32 position, i32 target, u32 duration, u32 group, u32 slot) {
    if (extra_icon(group, slot))
        target = (target / static_cast<i32>(slot)) * static_cast<i32>(sdk_lfo_icon_slot(slot, 1));
    return lfo::animate(controller, position, target, duration);
}

u32 sdk_lfo_icon_slot(u32 slot, u32 enabled) {
    if (!enabled || slot < page_controls || slot >= parameter_slots) return slot;
    const u32 track = lfo::selected_track();
    const LfoMachine* m = track < 8 ? machine(lfo::gui_engine(track)) : nullptr;
    return m ? m->controls[slot < m->parameters ? slot : m->parameters - 1] : slot;
}

const char* sdk_lfo_label(u32 group, u32 slot, u32 flag) {
    if (group == 0 && slot < parameter_slots) {
        const u32 track = lfo::selected_track();
        const u32 count = track < 8 ? extended(lfo::gui_engine(track)) : 0;
        if (count && slot >= count) slot = count - 1;
    }
    return lfo::name(group, slot, flag);
}

// 8c0b2d54: the instrument controller's encoder-click path publishes after
// its own four-way cycle, bypassing the turn quantizer. Read the unchanged
// selector and cycle its full slot before delegating the original publication.
u32 sdk_lfo_cycle(u32 project, u32 track, u32 index, i32 value) {
    const u32 count = target_count(track, index);
    if (count) {
        const i32 current = lfo::value(project, track, index);
        const u32 slot = current < 0 ? 0 : static_cast<u32>(current) >> 28;
        const u32 next = slot + 1 < count ? slot + 1 : 0;
        value = static_cast<i32>((next << 28) + 0x08000000u);
    }
    return lfo::publish(project, track, index, value);
}

// Element additionally hides its strip when slot > 3, before naming the
// destination. Restore visibility for extended destinations, including a
// retained selector that now resolves to the machine's last declared slot.
const char* sdk_lfo_element_label(u32 group, u32 slot, u32 flag, u32 widget) {
    if (extra_icon(group, slot)) lfo::show(widget);
    return sdk_lfo_label(group, slot, flag);
}

i32 sdk_lfo_step(u32 track, u32 index, i32 value, const i32* event) {
    const u32 count = event ? target_count(track, index) : 0;
    if (count) {
        i32 slot = value < 0 ? 0 : static_cast<u32>(value) >> 28;
        if (slot >= static_cast<i32>(count)) slot = count - 1;
        // Match stock's discrete detents and accelerated turns. Bound
        // before multiplying so even a bad event cannot overflow.
        i32 delta = event[1];
        if (delta > 8) delta = 8;
        if (delta < -8) delta = -8;
        slot += delta * (event[2] > 0 ? 2 : 1);
        if (slot < 0) slot = 0;
        if (slot >= static_cast<i32>(count)) slot = count - 1;
        return static_cast<i32>((static_cast<u32>(slot) << 28) + 0x08000000u);
    }
    return lfo::step(track, index, value, event);
}

// Only the engine-group modulation call is hooked. The caller keeps the
// original selector >> 8 in R4; its R2 has already lost the third slot bit.
// Read the audio engine table, never the GUI/project getters, on this path.
void sdk_lfo_modulate(u32 project, u32 track, u32 index, i32 amount, u32 selector_q23) {
    const u32 count = track < 8 ? extended(lfo::audio_engine(track)) : 0;
    if (count) {
        index = selector_q23 >> 20;
        if (index >= count) index = count - 1;
        // Stock saved the folded slot before this call. Its later telemetry
        // publication must name the same full slot as the audio modulation.
        lfo::modulation_slot(index);
    }
    lfo::modulate(project, track, index, amount);
}
