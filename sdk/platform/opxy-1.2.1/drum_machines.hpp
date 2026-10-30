// Owned synthesis kits using stock's per-key Drumline region transport.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
struct DrumMachine {
    const char* id;
    int (*ready)();
    u32 model_count;
    void (*defaults)(u32 key, u8* model, u8* controls);
    const char* (*label)(u32 model, u32 slot);
    const char* (*model_name)(u32 model);
    u32 (*count)(u32 model);
    // Optional named groups of consecutive models (resources.modelGroups):
    // Shift and the first encoder choose a group, the second a model in it.
    u32 group_count;
    const u8* group_sizes;
    const char* const* group_names;
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
    // A model's group (0 without groups) and the group's first model.
    u32 group(u32 model) const {
        u32 g = 0;
        for (u32 first = 0; g + 1 < group_count && model >= first + group_sizes[g]; ++g) first += group_sizes[g];
        return g;
    }
    u32 group_first(u32 g) const {
        u32 first = 0;
        for (u32 i = 0; i < g && i < group_count; ++i) first += group_sizes[i];
        return first;
    }
    // A slot the model uses: below its count and named. An unnamed slot inside
    // the count is a gap in the machine's own layout: blank and inert.
    bool used(u32 model, u32 slot) const {
        const char* name = slot < count(model) ? label(model, slot) : nullptr;
        return name && name[0];
    }
};
}}

extern "C" {
extern const opxy::platform::DrumMachine sdk_drum_machines[] __asm__("sdk_drum_machines");
extern const opxy::u32 sdk_drum_machine_count __asm__("sdk_drum_machine_count");
const opxy::platform::DrumMachine* sdk_drum_machine(opxy::u32 engine) __asm__("sdk_drum_machine");
opxy::u32 sdk_drum_alias(opxy::u32 engine) __asm__("sdk_drum_alias");
// GUI projection: the selected key's model and stored controls; track offsets
// (step locks, modulation) remain separate.
bool sdk_drum_values(opxy::u32 track, opxy::u8* model, opxy::u8* controls) __asm__("sdk_drum_values");
const char* sdk_drum_title(opxy::u32 engine, opxy::u32 slot) __asm__("sdk_drum_title");
// The selected key's canonical pad, 53..76, or 0 when the track is not an owned kit.
opxy::u32 sdk_drum_selected_pad(opxy::u32 track) __asm__("sdk_drum_selected_pad");
// The canonical pad a played note sounds on a track, 53..76, or 0. GUI only.
opxy::u32 sdk_drum_pad(opxy::u32 track, opxy::u32 note) __asm__("sdk_drum_pad");
// The selected key's choke group, as the Shift page names it.
const char* sdk_drum_choke(opxy::u32 track) __asm__("sdk_drum_choke");
opxy::u32 sdk_drum_edit(opxy::u32 key, opxy::u32 control, const opxy::u32* event, opxy::u32 fine)
    __asm__("sdk_drum_edit");
}
