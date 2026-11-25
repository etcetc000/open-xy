// A drum kit's value editing across models (resources.modelEdit, SDK 1.25). The kit's
// edit_model_parameter answers a turn of one of its controls with the key's model and that
// control's value as one position, model * 128 + value, so one encoder can run on into the
// next model past 127 and the previous one below 0. Its table (sdk_drum_model_edits, built at
// startup from the kits' records) holds rows only in a build with such a kit, which alone compiles
// the shared editor's call (OPXY_MODEL_EDIT, drum_synthesis_stock.cpp): every other kit's
// row, and every other build, stay as they were.
#pragma once
#include <opxy/types.hpp>
#include "drum_machines.hpp"

namespace opxy { namespace platform {
struct DrumModelEdit {
    const char* id;
    // The kit's edit_model_parameter: (model, slot, value 0..127, delta -127..127 not 0,
    // speed 0..127) to model * 128 + value.
    u32 (*edit)(u32 model, u32 slot, u32 value, i32 delta, u32 speed);
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
};
}}

extern "C" {
extern const opxy::platform::DrumModelEdit* sdk_drum_model_edits __asm__("sdk_drum_model_edits");
extern opxy::u32 sdk_drum_model_edit_count __asm__("sdk_drum_model_edit_count");
}

namespace opxy { namespace platform {
// The kit's model editor, or null for a kit without modelEdit.
inline const DrumModelEdit* drum_model_edit(const DrumMachine& machine) {
    for (u32 i = 0; i < sdk_drum_model_edit_count; ++i)
        if (sdk_drum_model_edits[i].engine() == machine.engine()) return &sdk_drum_model_edits[i];
    return nullptr;
}

// A modelEdit kit's turn of an available control `slot` (the shared editor's checks passed):
// asks the kit for the key's next position and, when it names another model or value within
// the kit, sets `model` and controls[slot]; the key's other controls stay, as Shift's MACHINE
// keeps them. False for no change: the kit's own position back, one outside
// 0..model_count * 128 - 1 (ignored, as parameterEdit ignores a value above 127), or one in a
// model that does not use `slot` (whose encoder would then turn nothing, and which a kit of
// more than 16 models might not store).
inline bool drum_model_turn(const DrumMachine& machine, const DrumModelEdit& kit, u32 slot, i32 delta,
                            i32 speed, u8& model, u8* controls) {
    if (speed < 0) speed = 0;
    if (speed > 127) speed = 127;
    const u32 now = static_cast<u32>(model) * 128 + controls[slot];
    const u32 next = kit.edit(model, slot, controls[slot], delta, static_cast<u32>(speed));
    if (next == now || next >= machine.model_count * 128 || !machine.used(next >> 7, slot)) return false;
    model = static_cast<u8>(next >> 7);
    controls[slot] = static_cast<u8>(next & 127);
    return true;
}
}}
