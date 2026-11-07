#include "live_input_transport.hpp"
#include "stock_browser.hpp"
#include "../extension_runtime.hpp"
extern "C" opxy::u32 sdk_live_input_option_bits() __asm__("sdk_live_input_option_bits");
extern "C" opxy::u32 stock_gui_role() __asm__("stock_gui_role");

namespace opxy { namespace platform { namespace input_monitor {
namespace {
const u32 kMonitorProject = 0x840a0524, kMonitorRecorder = 0x860c072c, kMonitorInputEnabled = 0x08023c48;
const u32 kGuiParameters = 0x86bd027c;
const char preference_path[] = "user/tilt-mic.cfg";
volatile u32 automatic;
bool owned, blocked, releasing, release_pending;
u32 project, epoch;
extensions::Time release_at;
stock_browser::StockSelector<2> selector = {};

// LIVE publication uses stock's existing parameter transport. Its stored
// counterpart is never touched; republish after stock clears its live values.
void publish(u32 value) {
    value |= sdk_live_input_option_bits();
    const u32 address = kGuiParameters + 0x4080 + (12 * 58 + word_index) * 4;
    if (word(address) != value)
        stock<void(u32,u32,u32,i32)>(0x8d5acea8)(kGuiParameters, 12, word_index, static_cast<i32>(value));
}
u32 marker(bool enabled) { return mark | (epoch << 1) | (enabled ? 1u : 0u); }
void status(const char* text) {
    if (byte(kSelectedTrack) == 12 && word(kMonitorProject) && !word(kMonitorProject + 4) && byte(kMonitorInputEnabled))
        extension_notify(text);
}
void release() {
    if (owned && stock<u32()>(0x8d4db490)()) stock<void(u32)>(0x8d4db4a4)(0);
    if (owned) {
        // The next stock controller update publishes the cleared monitor flag
        // before we release the envelope token. Releasing the token first would
        // briefly bypass the completed fade while audio still sees monitoring on.
        release_pending = true;
        if (automatic) status("tilt mic idle");
    }
    owned = releasing = false;
}
// Own bounded, versioned preference; invalid/missing data means Off.
bool preference(bool write, u32& choice) {
    u8 bytes[8] = {'O','X','T','M',1,static_cast<u8>(choice),0,0};
    bytes[6] = bytes[5] ^ 0xa5u;
    void* file = stock<void*(const char*,const char*)>(0x8fed0348)(preference_path, write ? "wb" : "rb");
    if (!file) return false;
    const u32 count = write
        ? stock<u32(const void*,u32,u32,void*)>(0x8fed0780)(bytes,1,sizeof(bytes),file)
        : stock<u32(void*,u32,u32,void*)>(0x8fed05bc)(bytes,1,sizeof(bytes),file);
    const int closed = stock<int(void*)>(0x8fed00ae)(file);
    if (closed || count != sizeof(bytes)) return false;
    if (bytes[0]!='O' || bytes[1]!='X' || bytes[2]!='T' || bytes[3]!='M' || bytes[4]!=1 ||
        bytes[5]>1 || bytes[6]!=(bytes[5]^0xa5u) || bytes[7]) return false;
    choice = bytes[5];
    return true;
}
void selected(void*, u32 choice) {
    if (choice > 1 || choice == automatic) return;
    if (!preference(true, choice)) { extension_notify("tilt mic save failed"); return; }
    automatic = choice;
    // The next controller update releases our lease and disarms the gesture.
}
}
}}}

using namespace opxy;
using namespace opxy::platform;
void sdk_input_monitor_startup() {
    if (!stock_gui_role() || !extensions_monitor_available()) return;
    u32 choice = 0;
    if (input_monitor::preference(false, choice)) input_monitor::automatic = choice;
}
bool sdk_input_monitor_menu() {
    if (!extensions_monitor_available()) return false;
    const char* labels[2] = {"off", "auto"};
    if (!input_monitor::selector.open("tilt mic", labels, input_monitor::automatic,
                                    {nullptr, input_monitor::selected, nullptr, nullptr})) return false;
    return true;
}

bool sdk_input_monitor_shift_visible() {
    return extensions_monitor_available() && byte(kSelectedTrack) == 12 &&
        word(0x0801eae4) == 0 && shift_held();
}
u32 sdk_input_monitor_choice() { return input_monitor::automatic; }
bool sdk_input_monitor_turn(u32 encoder, const i32* event) {
    if (encoder >= 8 || encoder % 4 || !sdk_input_monitor_shift_visible()) return false;
    if (event) {
        const i32 delta = event[0];
        if (delta) input_monitor::selected(nullptr, delta > 0 ? 1u : 0u);
    }
    return true;
}

namespace opxy { namespace platform {
extensions::InputMonitor extension_input_monitor() {
    using namespace input_monitor;
    const bool active = stock<u32()>(0x8d4db490)() != 0;
    bool available = automatic && word(kMonitorProject) && !word(kMonitorProject + 4) && byte(kMonitorInputEnabled) &&
        !byte(kMonitorRecorder) && !byte(kMonitorRecorder + 1);
    if (available) {
        const u32 source = stock<u32(u32,u32,u32)>(0x8d5ac910)(kGuiParameters, 12, 0);
        available = ((source >> 8) * 5u >> 23) == 0 && !stock<u32(u32)>(0x8d4dc450)(0);
    }
    return {active, owned && active, available};
}
bool extension_monitor_input(bool enabled) {
    using namespace input_monitor;
    if (release_pending) { publish(released); release_pending = false; }
    const extensions::InputMonitor input = extension_input_monitor();
    const extensions::Time now = extension_motion().time;
    if (owned && (!input.active || !input.available || project != word(kMonitorProject))) {
        release();
        blocked = true;
    }
    if (!enabled) {
        blocked = false;
        if (!owned) return true;
        if (!releasing) { releasing = true; release_at = now + release_us; }
        publish(marker(false));
        if (now >= release_at) release();
        return true;
    }
    if (blocked || !input.available) return false;
    if (owned) { releasing = false; publish(marker(true)); return true; }
    if (input.active) { blocked = true; return false; }
    project = word(kMonitorProject);
    epoch = (epoch + 1) & 31u;
    publish(marker(true));
    stock<void(u32)>(0x8d4db4a4)(1);
    owned = true;
    status("tilt mic live");
    return true;
}
}}
