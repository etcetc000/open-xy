// Host boundary double for the real synth control adapter's enum steps,
// labels and scramble: stock's step-count query, the engine it reads, stock's
// parameter set and its scramble generator. The machines are table rows; no
// machine behavior is tested.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <synth_controls.cpp>
// This fixture has no synthesis drum kits.
const opxy::platform::DrumMachine* sdk_drum_machine(opxy::u32) { return nullptr; }
bool sdk_drum_values(opxy::u32, opxy::u8*, opxy::u8*) { return false; }

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
// Stock's set, as the scramble calls it: each write, and a result the hook returns.
struct Write { u32 project, track, index; i32 value; };
Write writes[16];
u32 write_count;
u32 stock_set(u32 project, u32 track, u32 index, i32 value) {
    if (write_count < 16) writes[write_count] = {project, track, index, value};
    return 0x100 + write_count++;
}
// Stock's scramble generator: a marker of the slot it was asked for.
i32 stock_random(u32 index, u32 shaped) { return static_cast<i32>(0x1000 + index + (shaped << 8)); }
int ready() { return 1; }
int unready() { return 0; }
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
// slot 1 is continuous. Effect 8 has a four-choice enum in slot 1. Engine 41
// cycles position 0 through slot 4 and position 1 through slot 5, an empty
// placeholder; slots 6 and 7 are unused. Engine 42 is paged: every position
// cycles to its second-page slot 4..7, of which slot 6 is a placeholder.
// Engine 43 has engine 41's alternates but is not ready (unselected in a kit).
const opxy::platform::SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(40), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0x2, {3, 0, 17, 0, 0, 0, 0, 0}, format, false, false},
    {reinterpret_cast<const char*>(8), ready, {0, 1, 2, 3, 4, 5, 6, 7}, 0xd, {0, 4, 0, 0, 0, 0, 0, 0}, format, true, false},
    {reinterpret_cast<const char*>(41), ready, {4, 5, 2, 3, 0, 1, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, format, false, false, 0x20},
    {reinterpret_cast<const char*>(42), ready, {4, 5, 6, 7, 0, 1, 2, 3}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, format, false, true, 0x40},
    {reinterpret_cast<const char*>(43), unready, {4, 5, 2, 3, 0, 1, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, format, false, false},
    {nullptr, nullptr, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false}};
const opxy::u32 sdk_synth_control_count = 5;
}

opxy::platform::Address opxy::platform::host_stock_address(opxy::platform::Address address) {
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&stock_engine);
    if (address == 0x8c0b2468) return reinterpret_cast<Address>(&stock_steps);
    if (address == 0x8d5acf28) return reinterpret_cast<Address>(&stock_set);
    if (address == 0x8c0b5230) return reinterpret_cast<Address>(&stock_random);
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
    // Scramble: stock's last write passes through with its result; an owned
    // synth's reached alternates, but not its placeholder or unused slots, then
    // get an unshaped value from stock's generator. Other engines get nothing more.
    engines[3] = 41;
    require(sdk_synth_scramble(0x86bd027c, 3, 13, 77) == 0x100 && write_count == 2, "Scramble writes");
    require(writes[0].project == 0x86bd027c && writes[0].track == 3 && writes[0].index == 13 && writes[0].value == 77,
            "Stock write not passed through");
    require(writes[1].project == 0x86bd027c && writes[1].track == 3 && writes[1].index == 4 && writes[1].value == 0x1004,
            "Alternate not scrambled");
    engines[4] = 42;
    write_count = 0;
    require(sdk_synth_scramble(0x86bd027c, 4, 13, 77) == 0x100 && write_count == 4, "Paged scramble writes");
    require(writes[0].index == 13 && writes[0].value == 77, "Paged stock write not passed through");
    const u32 second[] = {4, 5, 7};
    for (u32 i = 0; i < 3; ++i)
        require(writes[i + 1].track == 4 && writes[i + 1].index == second[i] &&
                    writes[i + 1].value == static_cast<i32>(0x1000 + second[i]), "Second page not scrambled");
    engines[5] = 43;
    const u32 others[] = {2, 5, 9, 14};
    for (const u32 track : others) {
        write_count = 0;
        require(sdk_synth_scramble(0x86bd027c, track, 13, 5) == 0x100 && write_count == 1 && writes[0].track == track &&
                    writes[0].index == 13 && writes[0].value == 5, "Scramble without alternates not stock");
    }
    std::puts("synth steps ok");
    return 0;
}
