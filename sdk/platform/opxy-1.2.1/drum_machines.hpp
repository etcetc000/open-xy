// Owned synthesis kits using stock's per-key Drumline region transport.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
struct DrumMachine {
    const char* id;
    int (*ready)();
    u32 model_count;
    void (*defaults)(u32 key, u8* values);
    const char* (*label)(u32 model, u32 slot);
    const char* (*model_name)(u32 model);
    u32 (*count)(u32 model);
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
};
}}

extern "C" {
extern const opxy::platform::DrumMachine sdk_drum_machines[] __asm__("sdk_drum_machines");
extern const opxy::u32 sdk_drum_machine_count __asm__("sdk_drum_machine_count");
const opxy::platform::DrumMachine* sdk_drum_machine(opxy::u32 engine) __asm__("sdk_drum_machine");
opxy::u32 sdk_drum_alias(opxy::u32 engine) __asm__("sdk_drum_alias");
// GUI projection: raw, decoded pad values; track modulation remains separate.
bool sdk_drum_values(opxy::u32 track, opxy::u8* values) __asm__("sdk_drum_values");
const char* sdk_drum_title(opxy::u32 engine, opxy::u32 slot) __asm__("sdk_drum_title");
opxy::u32 sdk_drum_edit(opxy::u32 key, opxy::u32 control, const opxy::u32* event, opxy::u32 fine)
    __asm__("sdk_drum_edit");
}
