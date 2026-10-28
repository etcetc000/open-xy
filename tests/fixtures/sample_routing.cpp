// Execute the SDK routing bridges with stock calls replaced at the boundary.
#include <cstdlib>
#include <initializer_list>
static void host_barrier() {}
#include <sample_controls_stock.cpp>
#include <sample_machines.cpp>
using namespace opxy::platform;
static void check(bool ok) { if (!ok) std::abort(); }
static bool ownership = true;
static int ready() { return ownership ? 1 : 0; }
const SampleMachine sdk_sample_machines[] = {
    {reinterpret_cast<const char*>(46), SamplePage::drawing, ready, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 1},
    {reinterpret_cast<const char*>(47), SamplePage::drawing, ready, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0},
    {reinterpret_cast<const char*>(48), SamplePage::sampler, ready, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 1},
    {reinterpret_cast<const char*>(49), SamplePage::drums, ready, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0}
};
const opxy::u32 sdk_sample_machine_count = 4;
static unsigned current_engine, selected_track_byte, predicates, turns, regions;
static unsigned engine(unsigned track) { check(track == 0); return current_engine; }
static unsigned predicate(unsigned id) { ++predicates; const unsigned alias = sdk_sample_alias(id); return alias == 2 || alias == 30; }
static unsigned pitch_kind(unsigned) { return 9; }
static unsigned stock_region(unsigned, unsigned, const unsigned*, unsigned) { ++regions; return 91; }
extern "C" unsigned sdk_sample_edit(void*, unsigned, int, unsigned) { std::abort(); }
extern "C" unsigned sdk_synth_turn(unsigned project, unsigned track, unsigned slot, unsigned event) {
    check(project == 0x86bd027c && track == 0 && slot < 8 && !event); ++turns; return 73;
}
Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&engine);
    if (address == 0x8d5b79c0) return reinterpret_cast<Address>(&predicate);
    if (address == 0x8d5b79f0) return reinterpret_cast<Address>(&pitch_kind);
    if (address == kSelectedTrack) return reinterpret_cast<Address>(&selected_track_byte);
    if (address == 0x8d4dccd8) return reinterpret_cast<Address>(&stock_region);
    std::abort();
}
int main() {
    for (unsigned id : {46u, 47u}) {
        current_engine = id;
        check(sdk_sample_ui_engine(0) == (id == 46 ? 2u : 30u));
        check(sdk_sample_controller_engine(0) == id);
        check(sdk_sample_parameter_kind(id) == 0 && predicates == 0);
        check(sdk_sample_live_pitch_kind(id) == 0);
        check(sdk_sample_press_engine_owned(0, 4) == id);
        check(sdk_sample_note_route(id) == 1);
        for (unsigned slot = 0; slot < 8; ++slot)
            check(sdk_sample_region_control(60, slot, nullptr, 0) == 73);
    }
    check(turns == 16 && !regions);
    // The same region must survive every played key on single-sample tracks.
    for (unsigned id : {46u, 48u}) {
        current_engine = id;
        for (unsigned key = 0; key < 128; ++key) check(sdk_sample_key(0, key) == 0);
    }
    current_engine = 47;
    check(sdk_sample_key(0, 60) == 0xffffffffu); // nearest-region mapping stays stock
    for (unsigned id : {48u, 49u}) {
        current_engine = id;
        check(sdk_sample_ui_engine(0) == (id == 48 ? 2u : 30u));
        check(sdk_sample_controller_engine(0) == sdk_sample_ui_engine(0));
        check(sdk_sample_parameter_kind(id) == 1);
        check(sdk_sample_note_route(id) == (id == 49 ? 2u : 1u));
    }
    ownership = false;  // missing, conflicting or deselected registration
    for (unsigned id : {2u, 18u, 46u, 47u, 48u, 49u, 62u}) {
        current_engine = id;
        check(sdk_sample_alias(id) == id && sdk_sample_controller_engine(0) == id);
        check(sdk_sample_key(0, 60) == 0xffffffffu);
        check(sdk_sample_note_route(id) == 0 && !own_sample_page(id));
        const unsigned calls = predicates;
        check(sdk_sample_parameter_kind(id) == (id == 2 ? 1u : 0u) && predicates == calls + 1);
        check(sdk_sample_live_pitch_kind(id) == 9);
        check(sdk_sample_region_control(60, 0, nullptr, 0) == 91);
    }
    check(turns == 16 && regions == 7);
}
