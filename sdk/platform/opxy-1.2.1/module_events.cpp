// Shared module dispatch: each optional service owns only its own messages
// and gestures. The arranger forwards all remaining traffic to stock.
#include "sample_display_controls.hpp"
#include "sample_display_telemetry.hpp"
#include "arranger_bounce_remote.hpp"
extern "C" {
void sdk_arranger_raw_event(const void*) __asm__("sdk_arranger_raw_event");
void sdk_arranger_poll() __asm__("sdk_arranger_poll");
void sdk_module_raw_event(const void*) __asm__("sdk_module_raw_event");
void sdk_module_message(const void*, opxy::u32) __asm__("sdk_module_message");
void sdk_module_remote_poll() __asm__("sdk_module_remote_poll");
void sdk_module_gui_poll() __asm__("sdk_module_gui_poll");
}
void sdk_module_raw_event(const void* event) {
    if (!sdk_sample_display_key_event(event)) sdk_arranger_raw_event(event);
}
void sdk_module_message(const void* data, opxy::u32 bytes) {
    if (!sdk_sample_visual_message(data, bytes)) sdk_arranger_bounce_message(data, bytes);
}
void sdk_module_remote_poll() {
    sdk_arranger_bounce_remote_poll();
    sdk_sample_visual_poll();
}
void sdk_module_gui_poll() {
    sdk_arranger_poll();
    sdk_sample_visual_poll();
}
