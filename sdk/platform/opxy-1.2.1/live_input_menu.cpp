#include "live_input_transport.hpp"
#include "stock.hpp"
#include <opxy/live_input.hpp>

extern "C" {
const opxy::live_input::Controls* sdk_live_input_controls() __asm__("sdk_live_input_controls");
opxy::u32 sdk_live_input_page() __asm__("sdk_live_input_page");
bool sdk_live_input_toggle_page() __asm__("sdk_live_input_toggle_page");
bool sdk_live_input_turn(opxy::u32, const opxy::i32*) __asm__("sdk_live_input_turn");
const char* sdk_live_input_caption(opxy::u32, bool) __asm__("sdk_live_input_caption");
const char* sdk_live_input_page_hint() __asm__("sdk_live_input_page_hint");
void sdk_live_input_publish() __asm__("sdk_live_input_publish");
opxy::u32 sdk_live_input_option_bits() __asm__("sdk_live_input_option_bits");
}

namespace {
using namespace opxy;
using namespace opxy::platform;
using namespace live_input_transport;

constexpr u32 gui_parameters = 0x86bd027c;
constexpr u32 selected_page = 0x0801eae4;
u32 selected[live_input::row_count];
u32 automatic_scale;
bool initialized, extra_page;

const live_input::Controls* shift_controls() {
    const auto* controls = sdk_live_input_controls();
    if (controls && !initialized) {
        for (u32 row = 0; row < live_input::row_count; ++row) selected[row] = controls->rows[row].initial;
        initialized = true;
    }
    return controls;
}

bool has_extra(const live_input::Controls& controls) {
    return live_input::has_rows(controls, live_input::first_rows, live_input::row_count);
}

u32 row_for(u32 encoder) {
    return extra_page ? live_input::first_rows + encoder :
        encoder ? encoder - 1 : live_input::row_count;
}

u32 brain_scale() {
    // The stock Brain M1 renderer selects its manual scale triple or the
    // resolved triple returned by 8d58b324. Its keyboard uses the first word
    // as the pitch-class base and the third as the scale family (8d5ab190).
    constexpr u32 project = 0x840a0524, manual = 0x84eb5584, resolved = 0x860bf520;
    if (!word(project) || word(project + 4)) return 0;
    const bool follows_brain = byte(manual) || byte(manual + 8);
    const u32 source = follows_brain
        ? resolved + (byte(resolved) ? 0x0c : byte(resolved + 8) ? 0x40 : 0x74)
        : manual + 0x74;
    const u32 base = word(source), family = word(source + 8);
    if (base >= 12 || family >= 2) return 0;
    return 1 + base + 12 * family;
}

bool step(u32& value, u32 count, const i32* event) {
    if (event && event[0]) {
        if (event[0] > 0 && value + 1 < count) ++value;
        if (event[0] < 0 && value) --value;
        sdk_live_input_publish();
    }
    return true;
}

}

opxy::u32 sdk_live_input_page() {
    if (byte(kSelectedTrack) != track || word(selected_page) != 0 ||
        !shift_held() || !shift_controls()) return 0;
    return extra_page ? 2 : 1;
}

bool sdk_live_input_toggle_page() {
    if (!sdk_live_input_page() || !has_extra(*shift_controls())) return false;
    extra_page = !extra_page;
    return true;
}

const char* sdk_live_input_page_hint() {
    const auto* controls = shift_controls();
    if (!controls || !has_extra(*controls)) return nullptr;
    return extra_page ? controls->first_page_hint : controls->second_page_hint;
}

bool sdk_live_input_turn(opxy::u32 encoder, const opxy::i32* event) {
    if (encoder >= 8 || !sdk_live_input_page()) return false;
    const u32 row = row_for(encoder % 4);
    const auto& controls = *shift_controls();
    if (row >= live_input::row_count || !controls.rows[row].count) return false;
    return step(selected[row], controls.rows[row].count, event);
}

const char* sdk_live_input_caption(opxy::u32 encoder, bool label) {
    if (encoder >= live_input::second_rows) return nullptr;
    const u32 row = row_for(encoder);
    const auto* controls = shift_controls();
    if (!controls || row >= live_input::row_count || !controls->rows[row].count) return nullptr;
    const auto& spec = controls->rows[row];
    return label ? spec.label : spec.labels[selected[row]];
}

opxy::u32 sdk_live_input_option_bits() {
    const auto* controls = shift_controls();
    return controls ? option_bits(*controls, selected, automatic_scale) : 0;
}

void sdk_live_input_publish() {
    if (extra_page && !sdk_live_input_page()) extra_page = false;
    static bool configured;
    configured = configured || sdk_live_input_option_bits() != 0;
    if (!configured) return;
    automatic_scale = brain_scale();
    const u32 options = sdk_live_input_option_bits();
    const u32 address = gui_parameters + 0x4080 + (track * words + slot) * 4;
    const u32 previous = word(address);
    const u32 value = (monitoring(previous) ? previous & ~option_mask : tag) | options;
    if (previous != value)
        stock<void(u32, u32, u32, i32)>(0x8d5acea8)(gui_parameters, track, slot, static_cast<i32>(value));
}
