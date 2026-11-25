// Host boundary double for the real synth control adapter built with the last
// control touched (OPXY_CONTROL_INTERACTION, tooling.native.control_interaction_unit):
// stock's press, turn, parameter read, engine, project and pattern words. The
// machine is one table row; no machine behavior is tested.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define OPXY_CONTROL_INTERACTION 1
#include <synth_controls.cpp>
// This fixture has no synthesis drum kits, Drumline page or other services.
const opxy::platform::DrumMachine* sdk_drum_machine(opxy::u32) { return nullptr; }
bool sdk_drum_values(opxy::u32, opxy::u8*, opxy::u8*) { return false; }
void sdk_drum_pad_values(opxy::u32, opxy::i32*) { std::abort(); }
const void* sdk_drumline_get() { return nullptr; }
// The step-lock calls every build links (lock_handles.hpp). No step is ever held here
// (host_step), so a held lock is never read or written; the frame call does nothing.
extern "C" opxy::u32 sdk_project_locks_held_value(opxy::u32, opxy::u32, opxy::u32, opxy::i32*) { std::abort(); }
extern "C" opxy::u32 sdk_project_locks_held_set(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::i32) { std::abort(); }
extern "C" void sdk_project_locks_frame() {}

namespace {
using opxy::i32;
using opxy::u32;
using opxy::u8;

void require(bool condition, int line, const char* message) {
    if (!condition) { std::fprintf(stderr, "line %d: %s\n", line, message); std::exit(1); }
}
#define CHECK(x) require((x), __LINE__, #x)

const u32 kProjectBase = 0x90000000u;
u32 engines[8] = {73, 73, 73, 73, 73, 73, 73, 73}, engine_reads;
u32 controller = 0x8400dff8u, page, project_pointer = kProjectBase, selected, role = 1;
u32 press_calls, turn_calls, reads, last_slot, last_event;
i32 value = 123;
bool clamp;
// The project's pattern words the stock pattern getter requires (interaction_pattern_ready).
u32 pattern_count[8] = {1, 1, 1, 1, 1, 1, 1, 1};
u8 project[0x400];

u32 host_engine(u32 track) { CHECK(track < 8); ++engine_reads; return engines[track]; }
const u32* host_top() { return &controller; }
bool shift_down;
u32 host_shift() { return shift_down; }
u32 host_step() { return 0; }
void host_press(u32) { ++press_calls; }
u32 host_turn(u32 p, u32 track, u32 slot, u32 event) {
    CHECK(p == 0x86bd027cu && track < 8);
    ++turn_calls; last_slot = slot; last_event = event;
    if (!clamp) ++value;
    return 0x12345678;
}
i32 host_get(u32 p, u32 track, u32 slot, u32 flag) { CHECK(p == 0x86bd027cu && track < 8 && slot < 8 && flag == 1); ++reads; return value; }
int ready() { return 1; }
bool format(u32, i32, char*) { return false; }
}

extern "C" {
static const opxy::platform::SynthControls sdk_synth_controls_rows[] = {
    {reinterpret_cast<const char*>(73), ready, {4, 1, 2, 3, 0, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, format, false, false},
    {nullptr, nullptr, {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0, 0, 0, 0, 0, 0}, nullptr, false, false}};
const opxy::platform::SynthControls* sdk_synth_controls = sdk_synth_controls_rows;
opxy::u32 sdk_synth_control_count = 1;
}

opxy::platform::Address opxy::platform::host_stock_address(opxy::platform::Address address) {
    switch (address) {
    case 0x8d58781c: return reinterpret_cast<Address>(&host_engine);
    case 0x8c0944b0: return reinterpret_cast<Address>(&host_top);
    case 0x8d51f184: return reinterpret_cast<Address>(&host_shift);
    case 0x8d5b7e34: return reinterpret_cast<Address>(&host_step);
    case 0x8d51f1fc: return reinterpret_cast<Address>(&host_press);
    case 0x8d5ad5bc: return reinterpret_cast<Address>(&host_turn);
    case 0x8d5ac9dc: return reinterpret_cast<Address>(&host_get);
    case 0x080238bc: return reinterpret_cast<Address>(&page);              // the instrument page
    case 0x840a0524: return reinterpret_cast<Address>(&project_pointer);   // the project
    case 0x840a0534: return reinterpret_cast<Address>(&selected);          // the selected track
    case 0x86c05ca4: return reinterpret_cast<Address>(&role);              // the GUI role flag
    }
    for (u32 t = 0; t < 8; ++t)
        if (address == kProjectBase + 0x54cbcu + t * 0x53f44u) return reinterpret_cast<Address>(&pattern_count[t]);
    if (address >= kProjectBase && address < kProjectBase + sizeof(project))
        return reinterpret_cast<Address>(project + (address - kProjectBase));
    std::fprintf(stderr, "unexpected stock address %08lx\n", static_cast<unsigned long>(address));
    std::abort();
}

int main() {
    using opxy::control_interaction::detail::native_latest;
    auto rejected = [](u32 track) {
        const u32 before = engine_reads;
        CHECK(!native_latest(track).available);
        CHECK(engine_reads == before);  // no engine lookup before its preconditions hold
    };
    // A screen reset asks before the project and its patterns exist.
    project_pointer = 0; rejected(0); rejected(7);
    project_pointer = kProjectBase; pattern_count[0] = 0; rejected(0);
    pattern_count[0] = 2; project[6] = 7; project[0x98 + 33 * 7] = 2; rejected(0);
    project[0x98 + 33 * 7] = 1;
    CHECK(native_latest(0).available == 1);  // the scene's pattern
    project[0xc] = 1; project[0xe] = 2; rejected(0);
    project[0xe] = 1; project[0x98 + 33 * 7] = 255;
    CHECK(native_latest(0).available == 1);  // the track's own pattern; the scene's is not read
    project[0xc] = 0; project[0xd] = 1; project[0x98 + 33 * 7] = 1;
    CHECK(native_latest(0).available == 1);  // a higher mask bit does not select track 0
    rejected(8); role = 0; rejected(0); role = 1;
    std::memset(project, 0, sizeof(project)); pattern_count[0] = 1;

    // An idle baseline, then each physical turn with the value stock stored.
    opxy::control_interaction::Event e = native_latest(0);
    CHECK(e.available == 1 && e.serial == 0);
    CHECK(sdk_synth_turn(0x86bd027cu, 0, 2, 99) == 0x12345678);
    e = native_latest(0);
    CHECK(e.serial == 1 && e.physical_knob == 2 && e.parameter_index == 2 && e.normalized_value_at_event == 124 && reads == 1);
    clamp = true; sdk_synth_turn(0x86bd027cu, 0, 2, 99);
    e = native_latest(0);
    CHECK(e.serial == 2 && e.normalized_value_at_event == 124);  // a turn at the limit still counts
    value = 999;
    CHECK(native_latest(0).normalized_value_at_event == 124);  // automation is not a physical event
    // A press selects position 0's alternate (slot 4), then back.
    sdk_synth_press(0);
    e = native_latest(0);
    CHECK(e.serial == 3 && e.parameter_index == 4 && e.physical_knob == 0 && e.normalized_value_at_event == 999);
    sdk_synth_press(0);
    CHECK(native_latest(0).serial == 4 && native_latest(0).parameter_index == 0);
    // Tracks keep their own last event.
    sdk_synth_turn(0x86bd027cu, 1, 3, 17);
    CHECK(native_latest(1).serial == 5 && native_latest(0).serial == 4);
    CHECK(last_event == 17 && last_slot == 3 && turn_calls == 3 && press_calls == 2);
    // Other pages and controllers record nothing.
    page = 1; sdk_synth_turn(0x86bd027cu, 0, 2, 1); sdk_synth_press(0);
    CHECK(native_latest(0).serial == 4);
    page = 0; controller = 0; sdk_synth_press(0);
    CHECK(native_latest(0).serial == 4);
    // Another engine on the track: no event of the old one.
    controller = 0x8400dff8u; engines[0] = 74;
    sdk_synth_turn(0x86bd027cu, 0, 1, 1);
    CHECK(!native_latest(0).available);
    role = 0; CHECK(!native_latest(1).available);
    std::puts("control interaction ok");
    return 0;
}
