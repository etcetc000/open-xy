// OS1.2.1 GUI poll entry; stock runs first, outside a key/render callback.
#include "../extension_runtime.hpp"
#include "arrangement.hpp"
extern "C" void sdk_stock_extension_gui_poll() __asm__("sdk_stock_extension_gui_poll");
extern "C" void sdk_extension_gui_poll() __asm__("sdk_extension_gui_poll");
void sdk_extension_gui_poll() {
    sdk_stock_extension_gui_poll();
    opxy::platform::extensions_gui_poll();
    sdk_arrangement_poll();
}
