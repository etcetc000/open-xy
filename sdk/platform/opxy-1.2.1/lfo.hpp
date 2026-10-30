// Stock LFO engine destinations. The stored selector has eight equal Q31
// buckets; stock folds them into four for ordinary instrument engines.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
struct LfoMachine {
    const char* id;
    int (*ready)();  // record ownership only: safe on the audio role
    u32 parameters;
    u8 controls[parameter_slots];
};
// Stock boundaries, supplied separately so the routing is host-testable.
namespace lfo {
u32 gui_engine(u32 track);
u32 audio_engine(u32 track);
u32 type(u32 track);
u32 group_value(u32 track);
u32 selected_track();
u32 animate(u32 controller, u32 position, i32 target, u32 duration);
const char* name(u32 group, u32 slot, u32 flag);
void show(u32 widget);
i32 step(u32 track, u32 index, i32 value, const i32* event);
i32 value(u32 project, u32 track, u32 index);
u32 publish(u32 project, u32 track, u32 index, i32 value);
void modulation_slot(u32 index);
void modulate(u32 project, u32 track, u32 index, i32 amount);
}
}}

extern "C" {
extern const opxy::platform::LfoMachine sdk_lfo_machines[] __asm__("sdk_lfo_machines");
extern const opxy::u32 sdk_lfo_machine_count __asm__("sdk_lfo_machine_count");
opxy::u32 sdk_lfo_range(opxy::u32 track, opxy::u32 group, opxy::u32 stock_flag) __asm__("sdk_lfo_range");
opxy::u32 sdk_lfo_icon_slot(opxy::u32 slot, opxy::u32 enabled) __asm__("sdk_lfo_icon_slot");
const char* sdk_lfo_label(opxy::u32 group, opxy::u32 slot, opxy::u32 flag) __asm__("sdk_lfo_label");
opxy::u32 sdk_lfo_cycle(opxy::u32 project, opxy::u32 track, opxy::u32 index, opxy::i32 value) __asm__("sdk_lfo_cycle");
opxy::i32 sdk_lfo_step(opxy::u32 track, opxy::u32 index, opxy::i32 value, const opxy::i32* event) __asm__("sdk_lfo_step");
void sdk_lfo_modulate(opxy::u32 project, opxy::u32 track, opxy::u32 index,
                      opxy::i32 amount, opxy::u32 selector_q23) __asm__("sdk_lfo_modulate");
opxy::u32 sdk_lfo_icon_slide(opxy::u32 controller, opxy::u32 position, opxy::i32 target,
                            opxy::u32 duration, opxy::u32 group, opxy::u32 slot) __asm__("sdk_lfo_icon_slide");
const char* sdk_lfo_element_label(opxy::u32 group, opxy::u32 slot, opxy::u32 flag,
                                  opxy::u32 widget) __asm__("sdk_lfo_element_label");
}
