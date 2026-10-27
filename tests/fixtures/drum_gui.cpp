#include "stock.hpp"
#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "synth_controls.hpp"
#include <cstdlib>
#include <cstring>

extern "C" {
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
}
namespace {
using namespace opxy;
using namespace opxy::platform;
u32 track, key = 53, engine = 34, page, controller = 0x8400b208;
u32 publications, dirty, clicks, fallback, last_key, turned_slot;
int ownership = 1;
bool shifted, shifted_octave;
i32 regions[24][32];
void require(bool ok) { if (!ok) std::abort(); }
int ready() { return ownership; }
void defaults(u32 note, u8* values) {
    values[0] = static_cast<u8>((note - 53) % 2);
    for (u32 i = 1; i < 8; ++i) values[i] = static_cast<u8>(20 + i);
    values[7] = static_cast<u8>(note);
}
const char* label(u32 model, u32 slot) { return slot == 0 ? "model" : model == 0 ? "full" : "short"; }
const char* name(u32 model) { return model ? "short" : "full"; }
u32 count(u32 model) { return model ? 3 : 8; }
bool format(u32, i32, char*) { return false; }
u32 get_engine(u32 selected) { require(selected == track); return engine; }
u32 get_key(u32 selected) { require(selected == track); return key; }
u32 get_track() { return track; }
u32 get_shift() { return shifted; }
u32 canonical(u32 selected, u32 note) { require(selected == track); return shifted_octave && note == 53 ? 65 : note; }
const u32* top() { return &controller; }
void press(u32) { ++clicks; }
void* region(u32 selected, u32 note, u32* scratch, u32 nearest) {
    note = canonical(selected, note);
    require(selected == track && note >= 53 && note < 77 && !nearest);
    *scratch = note - 53;
    return regions[note - 53];
}
void changed() { ++dirty; }
void publish(u32 note) { ++publications; last_key = note; }
u32 stock_edit(u32, u32, const u32*, u32) { ++fallback; return 1234; }
u32 turn(u32, u32 selected, u32 slot, u32) { require(selected == track); turned_slot = slot; return slot; }
const char* title(u32, u32) { return "stock"; }
u32 text(u32, const char*) { return 0; }
void shown(u32 a, u32 b, u32 c, u32 d) {
    u8 slots[4]; sdk_synth_shown(track, slots);
    require(slots[0] == a && slots[1] == b && slots[2] == c && slots[3] == d);
}
}

extern "C" {
const opxy::platform::DrumMachine sdk_drum_machines[] = {
    {reinterpret_cast<const char*>(34), ready, 2, defaults, label, name, count}
};
const opxy::u32 sdk_drum_machine_count = 1;
const opxy::platform::SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(34), ready, {4,5,6,7,0,1,2,3}, 0, format, false, {5, 5, 5, 5, 5, 5, 5, 5}},
    {reinterpret_cast<const char*>(35), ready, {0,1,2,3,4,5,6,7}, 0xfa, format, false, {5, 0, 64, 2, 0, 0, 0, 128}}
};
const opxy::u32 sdk_synth_control_count = 2;
}

opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case kSelectedTrack: return reinterpret_cast<Address>(&track);
    case 0x08023c74: return reinterpret_cast<Address>(&page);
    case 0x8d585eb4: return reinterpret_cast<Address>(&get_engine);
    case 0x8d4d7650: return reinterpret_cast<Address>(&get_key);
    case 0x8d4d78cc: return reinterpret_cast<Address>(&get_track);
    case 0x8d51e5bc: return reinterpret_cast<Address>(&get_shift);
    case 0x8c095008: return reinterpret_cast<Address>(&top);
    case 0x8d51e634: return reinterpret_cast<Address>(&press);
    case 0x8d587904: return reinterpret_cast<Address>(&region);
    case 0x8d5878a8: return reinterpret_cast<Address>(&canonical);
    case 0x8d584584: return reinterpret_cast<Address>(&changed);
    case 0x8d4d68c0: return reinterpret_cast<Address>(&publish);
    case 0x8d4d6908: return reinterpret_cast<Address>(&stock_edit);
    case 0x8d5ab8ec: return reinterpret_cast<Address>(&turn);
    case 0x8d5065fc: return reinterpret_cast<Address>(&title);
    case 0x8d5ff080: return reinterpret_cast<Address>(&text);
    default: std::abort();
    }
}

int main() {
    using namespace opxy;
    using namespace opxy::platform;
    for (u32 pad = 0; pad < 24; ++pad) {
        regions[pad][0] = 123456;
        regions[pad][5] = 987654;
        reinterpret_cast<u8*>(regions[pad])[26] = static_cast<u8>(53 + pad);
    }
    shown(0,1,2,3);
    sdk_synth_press(1); shown(0,5,2,3);
    const u32 event[3] = {1000, 3, 2};
    sdk_drum_edit(53, 1, event, 0);
    u8 raw[8]; require(drum_unpack(regions[0] + 1, raw));
    require(raw[5] == 28 && raw[1] == 21 && publications == 1 && dirty == 1 && last_key == 53);
    shifted = true;
    sdk_drum_edit(53, 5, event, 0);
    require(drum_unpack(regions[0] + 1, raw) && raw[5] == 31 && raw[1] == 21);
    require(regions[0][0] == 123456 && regions[0][5] == 987654);
    sdk_synth_turn(0, 0, 5, 0);
    require(turned_slot == 5);
    shifted = false;

    key = 54; shown(0,1,2,3); // New pad's shorter model invalidates selected alternate.
    sdk_synth_press(1); shown(0,1,2,3);
    sdk_drum_edit(54, 3, event, 0); // A missing primary parameter stays blank/inert.
    require(publications == 2);
    require(!std::strcmp(sdk_drum_title(34, 1), "short"));
    key = 53;
    sdk_synth_press(1); shown(0,5,2,3);
    const u32 one[3] = {0, 1, 0};
    sdk_drum_edit(53, 0, one, 0); // Model changes on this pad also clear invalid alternate.
    shown(0,1,2,3);
    require(sdk_drum_values(0, raw) && raw[0] == 1);
    require(publications == 3 && last_key == 53);

    const u32 negative[3] = {0, static_cast<u32>(-100000), 0};
    sdk_drum_edit(53, 1, negative, 0);
    require(sdk_drum_values(0, raw) && raw[1] == 0);
    const u32 zero[3] = {};
    sdk_drum_edit(53, 1, zero, 0);
    require(publications == 4 && dirty == 4);

    // Parent note 53 can select canonical pad 65 in the other octave. GUI
    // defaults use the selected record's key, matching the audio mapper.
    shifted_octave = true;
    require(sdk_drum_values(0, raw) && raw[0] == 0);
    u8 expected[8]; defaults(65, expected);
    require(!std::memcmp(raw, expected, 8));
    // A missing record key uses the same stock mapping as playback.
    reinterpret_cast<u8*>(regions[12])[26] = 0;
    require(sdk_drum_values(0, raw) && !std::memcmp(raw, expected, 8));
    shifted_octave = false;

    ownership = 0;
    require(sdk_drum_edit(53, 0, one, 0) == 1234 && fallback == 1);
    require(!sdk_drum_values(0, raw));
    shown(0,1,2,3);
    sdk_synth_press(0); shown(0,1,2,3);
    require(clicks == 4);

    // An owned synth's enum moves a choice a detent (two when accelerated) and
    // lands on the choice's midpoint, which the machine reads as that choice.
    ownership = 1; engine = 35;
    const i32 up[3] = {1000, 1, 0}, down[3] = {-1000, -1, 0}, fast[3] = {3000, 1, 2}, still[3] = {0, 0, 0};
    const i32 lots[3] = {0, 0x7fffffff, 0}, fewer[3] = {0, -2147483647 - 1, 0};
    const u32 counts[4] = {5, 64, 2, 128}, slots[4] = {0, 2, 3, 7};
    for (u32 k = 0; k < 4; ++k) {
        const u32 n = counts[k], slot = slots[k];
        const auto choice = [n](i32 v) { return ((static_cast<u32>(v) >> 16) * n) >> 15; };
        i32 value = 0, stepped = -1;
        for (u32 c = 1; c < n; ++c) {
            require(sdk_synth_step(0, slot, value, up, stepped) && choice(stepped) == c);
            value = stepped;
        }
        require(sdk_synth_step(0, slot, value, up, stepped) && choice(stepped) == n - 1);
        require(sdk_synth_step(0, slot, value, down, stepped) && choice(stepped) == n - 2);
        require(sdk_synth_step(0, slot, 0, fast, stepped) && choice(stepped) == (n > 2 ? 2 : 1));
        require(sdk_synth_step(0, slot, 0, still, stepped) && choice(stepped) == 0);
        require(sdk_synth_step(0, slot, 0, lots, stepped) && choice(stepped) == (n > 8 ? 8 : n - 1));
        require(sdk_synth_step(0, slot, 0x7fffffff, fewer, stepped) && choice(stepped) == (n > 8 ? n - 9 : 0));
        require(sdk_synth_step(0, slot, -5, up, stepped) && choice(stepped) == 1);
    }
    i32 stepped = 0;
    require(!sdk_synth_step(0, 1, 0, up, stepped));          // continuous: stock steps it
    require(!sdk_synth_step(0, 0, 0, nullptr, stepped));
    require(!sdk_synth_step(0, 8, 0, up, stepped));
    require(!sdk_synth_step(8, 0, 0, up, stepped));
    engine = 34;
    require(!sdk_synth_step(0, 0, 0, up, stepped));          // a drum kit steps its own integers
    engine = 35; ownership = 0;
    require(!sdk_synth_step(0, 0, 0, up, stepped));
    engine = 36; ownership = 1;
    require(!sdk_synth_step(0, 0, 0, up, stepped));
    return 0;
}
