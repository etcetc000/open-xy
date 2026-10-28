// Host boundary double for the real synth control adapter's enum steps and
// labels: stock's step-count query and the engine it reads. The machines are
// table rows; no machine behavior is tested.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <synth_controls.cpp>

namespace {
using opxy::i32;
using opxy::u32;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

u32 engines[16], stock_asks, selected, page, controller = 0x8400dff8, shifted, edited_slot;

u32 stock_engine(u32 track) { return track < 16 ? engines[track] : 0; }
u32 stock_steps(u32 track, u32 index, u32* scale) {
    ++stock_asks;
    *scale = track + index;  // a marker the adapter must pass through untouched
    return 0xffffffff;
}
u32 stock_shift() { return shifted; }
const u32* stock_controller() { return &controller; }
void stock_press(u32) {}
u32 stock_edit(u32, u32, u32 slot, u32) { edited_slot = slot; return 71; }
int ready() { return 1; }
bool format(u32, i32, char*) { return false; }

// The value stock stores for choice `index` of `count`: the bucket's centre.
i32 centre(u32 index, u32 count) {
    const u32 q = 0x7fffffffu / count;
    return static_cast<i32>(index * q + q / 2);
}
}

extern "C" {
// Engine 40: a synth whose slot 0 is a three-choice enum,
// slot 2 a 17-choice enum and slot 3 a 256-choice one stock steps itself;
// slot 1 is continuous. Effect 8 has a four-choice enum in slot 1.
const opxy::platform::SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(40), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0x2, {3, 0, 17, 0, 0, 0, 0, 0}, format, false, false, 0, 0},
    {reinterpret_cast<const char*>(8), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0xd, {0, 4, 0, 0, 0, 0, 0, 0}, format, true, false, 0, 0},
    {reinterpret_cast<const char*>(41), ready, {0, 5, 6, 7, 4, 1, 2, 3}, 0xfe, {15, 0, 0, 0, 0, 0, 0, 0}, format, false, true, 0, 4},
    {nullptr, nullptr, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false, 0, 0}};
const opxy::u32 sdk_synth_control_count = 3;
}

opxy::platform::Address opxy::platform::host_stock_address(opxy::platform::Address address) {
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&stock_engine);
    if (address == 0x8c0b2468) return reinterpret_cast<Address>(&stock_steps);
    if (address == kSelectedTrack) return reinterpret_cast<Address>(&selected);
    if (address == 0x080238bc) return reinterpret_cast<Address>(&page);
    if (address == 0x8d51f184) return reinterpret_cast<Address>(&stock_shift);
    if (address == 0x8c0944b0) return reinterpret_cast<Address>(&stock_controller);
    if (address == 0x8d51f1fc) return reinterpret_cast<Address>(&stock_press);
    if (address == 0x8d5ad5bc) return reinterpret_cast<Address>(&stock_edit);
    std::abort();
}

int main() {
    engines[2] = 40;
    engines[14] = 8;
    u32 scale = 2;
    // Owned enum slots answer their choice count with scale 1.
    require(sdk_param_steps(2, 0, &scale) == 3 && scale == 1 && stock_asks == 0, "Synth enum not stepped");
    scale = 2;
    require(sdk_param_steps(2, 2, &scale) == 17 && scale == 1, "Second enum not stepped");
    require(sdk_param_steps(14, 1, &scale) == 4 && scale == 1, "Effect enum not stepped");
    // Continuous and oversized slots, other tracks and other engines ask stock.
    const u32 asked[][2] = {{2, 1}, {2, 3}, {2, 8}, {3, 0}, {14, 0}, {15, 1}, {9, 0}};
    for (const auto& a : asked) {
        scale = 2;
        const u32 before = stock_asks;
        require(sdk_param_steps(a[0], a[1], &scale) == 0xffffffff && stock_asks == before + 1 &&
                    scale == a[0] + a[1], "Stock step count not delegated");
    }

    // The label shown is the choice a stored value selects: every bucket's
    // centre and both edges, negative values first.
    const char* const labels[] = {"play", "wr a", "wr b"};
    char text[16];
    for (u32 index = 0; index < 3; ++index) {
        opxy::platform::control_label(text, centre(index, 3), labels, 3);
        require(std::strcmp(text, labels[index]) == 0, "Label not the stored choice");
    }
    opxy::platform::control_label(text, -1, labels, 3);
    require(std::strcmp(text, "play") == 0, "Negative value not the first label");
    opxy::platform::control_label(text, 0x7fffffff, labels, 3);
    require(std::strcmp(text, "wr b") == 0, "Full value not the last label");
    for (u32 count = 2; count <= 256; ++count)
        for (u32 index = 0; index < count; ++index)
            require(opxy::platform::control_choice(centre(index, count), count) == index, "Choice not the stored bucket");
    // Real platform routing: opt-in shift selector, both pages, per-track reset.
    selected = 0; engines[0] = 41; opxy::u8 shown[4];
    for (u32 row = 0; row < 2; ++row) {
        sdk_synth_shown(0, shown);
        require(shown[0] == 4 && shown[1] == (row ? 5u : 1u), "Wrong selector page");
        shifted = 0;
        require(sdk_synth_turn(0, 0, 0, 0) == 71 && edited_slot == 4, "Plain black must edit normal slot");
        shifted = 1;
        require(sdk_synth_turn(0, 0, 4, 0) == 71 && edited_slot == 0, "Shift black must edit selector");
        for (u32 col = 0; col < 4; ++col) {
            const auto edit = sdk_synth_controls[2].edit(col, true, shown);
            require(edit.slot == (col ? shown[col] : 0) && edit.fine == (col != 0), "Other encoders lost fine editing");
            if (col) { sdk_synth_turn(0, 0, col+4, 0); require(edited_slot == shown[col], "Fine edit routed to wrong page"); }
        }
        sdk_synth_press(2);
    }
    selected = 1; engines[1] = 41; sdk_synth_shown(1, shown);
    require(shown[0] == 4 && shown[1] == 1, "Page selection leaked between tracks");
    engines[0] = 40; sdk_synth_shown(0, shown);
    require(shown[0] == 0, "Opt-out synth lost original initial selection");
    const auto legacy = sdk_synth_controls[0].edit(1, true, shown);
    require(legacy.slot == 1 && legacy.fine, "Legacy fine adjustment changed");
    shifted = 0; sdk_synth_turn(0, 3, 2, 0);
    require(edited_slot == 2, "Stock engine turn changed");
    page = 1; shifted = 1; sdk_synth_turn(0, 0, 4, 0);
    require(edited_slot == 4, "Other instrument page changed");
    std::puts("synth steps ok");
    return 0;
}
