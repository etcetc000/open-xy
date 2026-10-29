#include "../../sdk/platform/opxy-1.2.1/module_events.cpp"
#include <cstdlib>
namespace { unsigned calls[8], count; bool key_owned, message_owned; }
void require(bool value) { if (!value) std::abort(); }
void append(unsigned id) { calls[count++] = id; }
bool sdk_sample_display_key_event(const void*) { append(1); return key_owned; }
void sdk_arranger_raw_event(const void*) { append(2); }
bool sdk_sample_visual_message(const void*, opxy::u32) { append(3); return message_owned; }
void sdk_arranger_bounce_message(const void*, opxy::u32) { append(4); }
void sdk_arranger_bounce_remote_poll() { append(5); }
void sdk_arranger_poll() { append(6); }
void sdk_sample_visual_poll() { append(7); }
int main() {
    sdk_module_raw_event(nullptr); require(count == 2 && calls[0] == 1 && calls[1] == 2);
    count = 0; key_owned = true; sdk_module_raw_event(nullptr); require(count == 1 && calls[0] == 1);
    count = 0; sdk_module_message(nullptr, 0); require(count == 2 && calls[0] == 3 && calls[1] == 4);
    count = 0; message_owned = true; sdk_module_message(nullptr, 0); require(count == 1 && calls[0] == 3);
    count = 0; sdk_module_remote_poll(); require(count == 2 && calls[0] == 5 && calls[1] == 7);
    count = 0; sdk_module_gui_poll(); require(count == 2 && calls[0] == 6 && calls[1] == 7);
}
