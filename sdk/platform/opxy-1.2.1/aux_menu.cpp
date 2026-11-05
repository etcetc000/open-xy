#include "stock.hpp"
extern "C" {
void sdk_aux_shift_m1() __asm__("sdk_aux_shift_m1");
void sdk_aux_machine_menu() __asm__("sdk_aux_machine_menu");
bool sdk_input_monitor_menu() __asm__("sdk_input_monitor_menu");
}
void sdk_aux_shift_m1() {
    if (opxy::platform::byte(opxy::platform::kSelectedTrack) == 12) {
        sdk_input_monitor_menu();
        return;
    }
    sdk_aux_machine_menu();
}
