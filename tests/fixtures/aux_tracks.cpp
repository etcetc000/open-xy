// Execute the actual aux replacement dispatch against stock boundary doubles.
#include <cstdio>
#include <cstdlib>
#include <aux_tracks.cpp>

namespace {
const u32 host_project = 0x90000000;
u32 host_project_word = host_project, host_tab, host_key, host_key_track;
unsigned char host_selected = 11;
unsigned char host_data[0xd7c + 16 * 0x53f44];
i32 host_live[16][control::track_parameters];
u32 host_turns, host_turn_track, host_turn_index, host_turn_event;
u32 host_telemetry, host_publishes;
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
u32 host_block(u32 track) { return host_project + 0xd7c + track * 0x53f44; }
u32& host_engine(u32 track) { return *reinterpret_cast<u32*>(host_data + host_block(track) - host_project + 0x18); }
i32& host_base(u32 track, u32 index) {
    return *reinterpret_cast<i32*>(host_data + host_block(track) - host_project + 0x3864 + index * 4);
}
void host_set_engine(u32 track, u32 engine) { host_engine(track) = engine; }
void host_set(u32, u32 track, u32 index, i32 value) { host_base(track, index) = host_live[track][index] = value; }
void host_live_set(u32, u32 track, u32 index, i32 value) { host_live[track][index] = value; }
void host_tab_set(u32 controller, u32 tab) { require(controller == kAuxController, "Wrong controller"); host_tab = tab; }
int host_ready() { return 1; }
void host_lfo_publish(u32, u32, i32, i32) { ++host_publishes; }
u32 host_telemetry_set(u32 id, u32 value) {
    require(id == 25, "Wrong aux telemetry id"); host_telemetry = value; return 1;
}
}
extern "C" {
const ControlMachine sdk_control_machines[] = {
    {reinterpret_cast<const char*>(40), host_ready, nullptr, nullptr, "plain", 0, nullptr, nullptr,
     true, 1u << 11, reinterpret_cast<const char*>(120), {10, 20, 30, 40}, false},
    {reinterpret_cast<const char*>(41), host_ready, nullptr, nullptr, "lfo", 0, nullptr, nullptr,
     true, 1u << 11, reinterpret_cast<const char*>(121), {11, 21, 31, 41}, true}
};
const u32 sdk_control_machine_count = 2;
AuxService sdk_aux;
u32 (*sdk_aux_engine)(u32);
bool (*sdk_aux_owns_page)(u32);
void sdk_synth_queue_page_key(u32 track, u32 key) { host_key_track = track; host_key = key; }
u32 sdk_effect_turn(u32 project, u32 track, u32 index, u32 event) {
    require(project == kProject, "Wrong editor project");
    ++host_turns; host_turn_track = track; host_turn_index = index; host_turn_event = event;
    return 1;
}
}
namespace opxy { namespace platform { namespace stock_browser { namespace detail {
bool open(Session&, Row*, u32, const char*, const char* const*, u32, Callbacks, i32) { return false; }
bool open_rows(Session&, Row*, u32, const char*, u32, Callbacks, i32, bool) { return false; }
bool name(String&, const char*) { return false; }
}}}}
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if (address == kSelectedTrack) return reinterpret_cast<Address>(&host_selected);
    if (address == kAuxTab) return reinterpret_cast<Address>(&host_tab);
    if (address == 0x840a0524) return reinterpret_cast<Address>(&host_project_word);
    if (address >= host_project && address < host_project + sizeof(host_data))
        return reinterpret_cast<Address>(host_data) + address - host_project;
    const u32 live_address = kProject + 0x4080;
    if (address >= live_address && address < live_address + sizeof(host_live))
        return reinterpret_cast<Address>(host_live) + address - live_address;
    if (address == 0x8d587708) return reinterpret_cast<Address>(&host_block);
    if (address == 0x8d5877e4) return reinterpret_cast<Address>(&host_set_engine);
    if (address == 0x8d5acf28) return reinterpret_cast<Address>(&host_set);
    if (address == 0x8d5acea8) return reinterpret_cast<Address>(&host_live_set);
    if (address == 0x8c08f74c) return reinterpret_cast<Address>(&host_tab_set);
    if (address == 0x8d4d68b8) return reinterpret_cast<Address>(&host_lfo_publish);
    if (address == 0x8d4d666a) return reinterpret_cast<Address>(&host_telemetry_set);
    std::fprintf(stderr, "Unmapped stock address\n"); std::abort();
}

int main() {
    sdk_lfo_aux_publish(3, 1, 0x40000000, 0x40000000);
    require(host_publishes == 1 && host_telemetry == 0, "Unowned track published aux telemetry");
    sdk_lfo_aux_publish(11, 1, 0x40000000, 0x40000000);
    require(host_publishes == 2 && host_telemetry == 0xab012000, "Positive LFO telemetry changed");
    sdk_lfo_aux_publish(11, 2, -0x40000000, 0x40000000);
    require(host_publishes == 3 && host_telemetry == 0xab02e000, "Negative LFO telemetry changed");
    host_engine(11) = 18;
    for (u32 i = 0; i < 8; ++i) host_base(11, i) = 100 + i;
    u32 event = 7;
    require(sdk_aux_page() == -1 && !sdk_aux_key(kAuxController, &event) && !sdk_aux_turn(0, 0, 123),
            "Stock CV did not retain its own dispatch");
    take(11, 1);
    require(host_engine(11) == 40 && host_base(11, 0) == 10 && host_live[11][57] == kAuxMark,
            "Takeover did not apply the machine's defaults and silencer");
    // No hidePages metadata: every inherited tab still routes to this machine.
    for (u32 tab = 0; tab < 4; ++tab) {
        host_tab = tab;
        require(sdk_aux_page() == 120 && owns_page(11), "Inherited CV page escaped ownership");
        require(sdk_aux_turn(0, 5, 123) && host_turn_track == 11 && host_turn_index == 1 && host_turn_event == 123,
                "Inherited CV tab changed encoder routing");
        event = 10;
        require(sdk_aux_key(kAuxController, &event) == 2, "CV received a machine encoder click or lost its handled result");
    }
    host_tab = 0;
    for (event = 6; event <= 9; ++event) {
        require(sdk_aux_key(kAuxController, &event) && host_key == event - 5 && host_key_track == 11 && host_tab == 0,
                "Machine did not receive its page key directly");
    }
    event = 20;
    require(!sdk_aux_key(kAuxController, &event), "Unrelated key intercepted");
    take(11, 2);
    host_tab = 3; event = 9;
    const u32 turns_before = host_turns;
    require(sdk_aux_page() == -1 && !owns_page(11) && !sdk_aux_key(kAuxController, &event) &&
            !sdk_aux_turn(0, 1, 123) && host_turns == turns_before, "Explicit shared LFO was intercepted");
    event = 7;
    require(sdk_aux_key(kAuxController, &event) && host_key == 2 && host_tab == 3,
            "M2 was lost on the shared LFO page");
    event = 6;
    require(sdk_aux_key(kAuxController, &event) && host_tab == 0 && sdk_aux_page() == 121,
            "M1 did not return to the replacement");
    take(11, 0);
    for (u32 i = 0; i < 8; ++i) require(host_base(11, i) == static_cast<i32>(100 + i), "CV's controls not restored");
    require(host_engine(11) == 18 && sdk_aux_page() == -1, "CV ownership not restored");
    host_selected = 14;
    require(sdk_aux_page() == -1 && !sdk_aux_key(kAuxController, &event) && !sdk_aux_turn(0, 1, 123),
            "Send track dispatch changed");
    return 0;
}
