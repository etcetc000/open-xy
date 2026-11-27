// An input overlay can expose processor controls without a monitor owner.
#include "input_monitor.hpp"
#include "../extension_runtime.hpp"

bool sdk_input_monitor_menu() { return false; }
bool sdk_input_monitor_shift_visible() { return false; }
opxy::u32 sdk_input_monitor_choice() { return 0; }
bool sdk_input_monitor_turn(opxy::u32, const opxy::i32*) { return false; }

namespace opxy { namespace platform {
extensions::InputMonitor extension_input_monitor() { return {}; }
bool extension_monitor_input(bool) { return false; }
}}
