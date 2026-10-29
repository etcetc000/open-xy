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

u32 engines[16], stock_asks;

u32 stock_engine(u32 track) { return track < 16 ? engines[track] : 0; }
u32 stock_steps(u32 track, u32 index, u32* scale) {
    ++stock_asks;
    *scale = track + index;  // a marker the adapter must pass through untouched
    return 0xffffffff;
}
bool registered = true;
int ready() { return registered ? 1 : 0; }
u32 selected = 2, page = 0, controller = 0x8400dff8, clicks = 0, writes = 0, actions = 0;
i32 saved_words[16][58] = {};
u32* stock_controller() { return &controller; }
void stock_click(u32) { ++clicks; }
void stock_set(u32 project, u32 track, u32 slot, i32 value) {
    require(project == 0x86bd027c && track == selected && slot < 4, "Action escaped its saved controls");
    saved_words[track][slot] = value;
    ++writes;
}
void press(u32 encoder, i32* parameters) {
    ++actions;
    parameters[0] += 65536;
    if (encoder == 1) parameters[1] = -1;
    if (encoder == 2) parameters[7] = 1;
}
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
    {reinterpret_cast<const char*>(40), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0x2, {3, 0, 17, 0, 0, 0, 0, 0}, format, false, false, press},
    {reinterpret_cast<const char*>(8), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0xd, {0, 4, 0, 0, 0, 0, 0, 0}, format, true, false, nullptr},
    {nullptr, nullptr, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false, nullptr}};
const opxy::u32 sdk_synth_control_count = 2;
}

opxy::platform::Address opxy::platform::host_stock_address(opxy::platform::Address address) {
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&stock_engine);
    if (address == 0x8c0b2468) return reinterpret_cast<Address>(&stock_steps);
    if (address == 0x8d51f1fc) return reinterpret_cast<Address>(&stock_click);
    if (address == 0x8c0944b0) return reinterpret_cast<Address>(&stock_controller);
    if (address == 0x8d5acf28) return reinterpret_cast<Address>(&stock_set);
    if (address == 0x840a0534) return reinterpret_cast<Address>(&selected);
    if (address == 0x080238bc) return reinterpret_cast<Address>(&page);
    if (address >= 0x86bd027c + 0x4080 && address < 0x86bd027c + 0x4080 + sizeof(saved_words))
        return reinterpret_cast<Address>(saved_words) + address - (0x86bd027c + 0x4080);
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
    saved_words[2][0] = 123;
    sdk_synth_press(0);
    require(writes == 1 && actions == 1 && saved_words[2][0] == 65659, "Action not saved through stock");
    sdk_synth_press(1);
    sdk_synth_press(2);
    require(writes == 1 && saved_words[2][0] == 65659, "Partial invalid edit was saved");
    const u32 before = actions;
    page = 1; sdk_synth_press(0); page = 0;
    registered = false; sdk_synth_press(0); registered = true;
    selected = 8; sdk_synth_press(0); selected = 2;
    sdk_synth_press(4);
    require(actions == before && writes == 1 && clicks == 7, "Action escaped ownership/page/encoder guard");
    std::puts("synth steps and saved actions ok");
    return 0;
}
