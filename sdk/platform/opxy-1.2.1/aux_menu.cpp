#include "stock.hpp"
extern "C" {
bool sdk_live_input_turn(opxy::u32, const opxy::i32*) __asm__("sdk_live_input_turn");
bool sdk_live_input_toggle_page() __asm__("sdk_live_input_toggle_page");
void sdk_aux_shift_m1() __asm__("sdk_aux_shift_m1");
void sdk_aux_machine_menu() __asm__("sdk_aux_machine_menu");
opxy::u32 sdk_aux_machine_turn(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_aux_machine_turn");
bool sdk_input_monitor_turn(opxy::u32, const opxy::i32*) __asm__("sdk_input_monitor_turn");
opxy::u32 sdk_aux_turn(opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_aux_turn");
bool sdk_input_monitor_menu() __asm__("sdk_input_monitor_menu");
}
void sdk_aux_shift_m1() {
    if (opxy::platform::byte(opxy::platform::kSelectedTrack) == 12) {
        if (!sdk_live_input_toggle_page()) sdk_input_monitor_menu();
        return;
    }
    sdk_aux_machine_menu();
}

opxy::u32 sdk_aux_turn(opxy::u32 controller, opxy::u32 encoder, opxy::u32 event) {
    const auto* turn = reinterpret_cast<const opxy::i32*>(event);
    if (sdk_live_input_turn(encoder, turn) || sdk_input_monitor_turn(encoder, turn)) return 1;
    return sdk_aux_machine_turn(controller, encoder, event);
}
