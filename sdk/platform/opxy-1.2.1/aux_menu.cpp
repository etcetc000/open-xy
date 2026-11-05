#include "stock.hpp"
extern "C" {
void sdk_aux_shift_m1() __asm__("sdk_aux_shift_m1");
void sdk_aux_machine_menu() __asm__("sdk_aux_machine_menu");
opxy::u32 sdk_aux_machine_turn(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_aux_machine_turn");
bool sdk_input_monitor_turn(opxy::u32, const opxy::i32*) __asm__("sdk_input_monitor_turn");
opxy::u32 sdk_aux_turn(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_aux_turn");
bool sdk_input_monitor_menu() __asm__("sdk_input_monitor_menu");
}
void sdk_aux_shift_m1() {
    if (opxy::platform::byte(opxy::platform::kSelectedTrack) == 12) {
        sdk_input_monitor_menu();
        return;
    }
    sdk_aux_machine_menu();
}

opxy::u32 sdk_aux_turn(opxy::u32 controller, opxy::u32 encoder, opxy::u32 event) {
    if (sdk_input_monitor_turn(encoder, reinterpret_cast<const opxy::i32*>(event))) return 1;
    return sdk_aux_machine_turn(controller, encoder, event);
}
