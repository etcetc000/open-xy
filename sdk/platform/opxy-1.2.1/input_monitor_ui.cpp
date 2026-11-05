// GUI-only Shift control, painted over the external-input page by the shared
// compositor traversal. Later stock dialogs and alerts retain their order.
#include "input_monitor.hpp"
#include "screen.hpp"
using namespace opxy;
using namespace opxy::screen;
namespace {
struct Overlay {
    struct Object { Base base; };
};
Overlay::Object overlay;
bool constructed;
void init(Overlay::Object*) {}
void frame(Overlay::Object*, i32) {}
const VTable<Overlay> methods = {&destroy<Overlay::Object>, &destroy<Overlay::Object>,
    reinterpret_cast<const void*>(0x8d51fcd4), frame, init,
    reinterpret_cast<const void*>(0x8c0867f0)};
}
void sdk_input_monitor_frame(u32 painted, u32 selected) {
    if (painted != 63 || selected != 63 || !sdk_input_monitor_shift_visible()) return;
    if (!constructed) {
        base_construct(&overlay.base, 0, "tilt_mic.svg");
        overlay.base.vtable = &methods;
        constructed = true;
    }
    if (!overlay.base.svg) stock<void(Base*)>(0x8d520ce0)(&overlay.base);
    if (!overlay.base.svg) return;
    if (Leaf* value = text_node(find(&overlay.base, "mode")))
        set_text(value, sdk_input_monitor_choice() ? "auto" : "off");
    draw(&overlay.base);
}
