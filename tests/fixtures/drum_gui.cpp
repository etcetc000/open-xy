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
void defaults(u32 note, u8* model, u8* values) {
    *model = static_cast<u8>((note - 53) % 2);
    for (u32 i = 0; i < 8; ++i) values[i] = static_cast<u8>(20 + i);
    values[7] = static_cast<u8>(note);
}
// The full model leaves slots 2 and 7 unnamed: gaps inside its count.
const char* label(u32 model, u32 slot) { return model ? "short" : slot == 2 || slot == 7 ? "" : "full"; }
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
// The event is a small token or, for a turn the SDK rewrote, a pointer to three words.
i32 turned_detents = -1;
u32 turn(u32, u32 selected, u32 slot, __UINTPTR_TYPE__ event) {
    require(selected == track); turned_slot = slot;
    turned_detents = event > 0xffff ? reinterpret_cast<const i32*>(event)[0] | reinterpret_cast<const i32*>(event)[1] : -1;
    return slot;
}
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
    {reinterpret_cast<const char*>(35), ready, {0,1,2,3,4,5,6,7}, 0xf8, format, false, {5, 0, 64, 2, 0, 0, 0, 128}, 0x02}
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
    u32 presses = 0;
    shown(0,1,2,3);
    sdk_synth_press(1); ++presses; shown(0,5,2,3);
    const u32 event[3] = {1000, 3, 2};
    sdk_drum_edit(53, 1, event, 0);   // the second encoder's alternate, slot 5
    u8 model = 99, raw[8];
    const u8* record = reinterpret_cast<const u8*>(regions[0]);
    require(drum_marked(record[24]) && drum_unpack(regions[0] + 1, &model, raw));
    require(model == 0 && raw[5] == 28 && raw[1] == 21 && raw[0] == 20 && raw[7] == 53);
    require(publications == 1 && dirty == 1 && last_key == 53);
    shifted = true;
    // Shift and the second encoder choose the choke group: none, then 1..3.
    // The edit above kept stock's play mode 0, gate, which reads as none.
    require(drum_choke(record[24]) == 0 && !std::strcmp(sdk_drum_choke(0), "NONE"));
    sdk_drum_edit(53, 5, event, 0);
    require(publications == 2 && (record[24] >> 4) == 5 && !std::strcmp(sdk_drum_choke(0), "3"));
    sdk_drum_edit(53, 5, event, 0);   // mute3 is the limit
    require(publications == 2);
    const u32 none[3] = {0, static_cast<u32>(-5), 0};
    sdk_drum_edit(53, 5, none, 0);
    require(publications == 3 && drum_marked(record[24]) && (record[24] >> 4) == 1);
    require(!std::strcmp(sdk_drum_choke(0), "NONE"));
    // Shift and the third and fourth encoders turn nothing.
    sdk_drum_edit(53, 6, event, 0);
    sdk_drum_edit(53, 7, event, 0);
    require(publications == 3);
    require(drum_unpack(regions[0] + 1, &model, raw) && raw[5] == 28 && raw[1] == 21 && model == 0);
    require(regions[0][0] == 123456 && regions[0][5] == 987654 && record[26] == 53);
    sdk_synth_turn(0, 0, 5, 0);
    require(turned_slot == 5);
    // A gap: the third encoder's slot 2 is blank and inert; its alternate,
    // slot 6, is used. The fourth's alternate, slot 7, is a gap: no toggle.
    shifted = false;
    sdk_drum_edit(53, 2, event, 0);
    require(publications == 3 && drum_unpack(regions[0] + 1, &model, raw) && raw[2] == 22);
    sdk_synth_press(3); ++presses; shown(0,5,2,3);
    sdk_synth_press(2); ++presses; shown(0,5,6,3);
    sdk_synth_press(2); ++presses; shown(0,5,2,3);

    // Shift and the first encoder choose the model, as on Drumline; the
    // controls stay, and the shorter model returns its missing alternate.
    const u32 one[3] = {0, 1, 0};
    sdk_drum_edit(53, 4, one, 0);
    require(sdk_drum_values(0, &model, raw) && model == 1 && raw[5] == 28 && publications == 4);
    shown(0,1,2,3);
    sdk_drum_edit(53, 4, event, 0);   // the last model is the limit
    require(publications == 4);
    shifted = false;

    key = 54; shown(0,1,2,3);
    sdk_synth_press(1); ++presses; shown(0,1,2,3);   // the shorter model has no slot 5
    sdk_drum_edit(54, 3, event, 0);   // nor a slot 3: inert
    require(publications == 4);
    require(!std::strcmp(sdk_drum_title(34, 1), "short"));
    key = 53;
    const u32 back[3] = {0, static_cast<u32>(-1), 0};
    shifted = true;
    sdk_drum_edit(53, 4, back, 0);
    shifted = false;
    require(sdk_drum_values(0, &model, raw) && model == 0 && publications == 5);
    sdk_synth_press(1); ++presses; shown(0,5,2,3);

    const u32 negative[3] = {0, static_cast<u32>(-100000), 0};
    sdk_drum_edit(53, 0, negative, 0);   // the first encoder edits slot 0 now
    require(sdk_drum_values(0, &model, raw) && raw[0] == 0 && model == 0);
    const u32 zero[3] = {};
    sdk_drum_edit(53, 1, zero, 0);
    require(publications == 6 && dirty == 6);

    // Parent note 53 can select canonical pad 65 in the other octave. GUI
    // defaults use the selected record's key, matching the audio mapper.
    shifted_octave = true;
    u8 expected_model, expected[8];
    defaults(65, &expected_model, expected);
    require(sdk_drum_values(0, &model, raw) && model == expected_model && !std::memcmp(raw, expected, 8));
    // A missing record key uses the same stock mapping as playback.
    reinterpret_cast<u8*>(regions[12])[26] = 0;
    require(sdk_drum_values(0, &model, raw) && !std::memcmp(raw, expected, 8));
    // A pad stored by the earlier eight-value layout (tag 10, unmarked) and a
    // marked pad with a model the kit lacks both keep the key's defaults.
    regions[12][1] = 0x2a000000; regions[12][2] = 0x15550000; regions[12][3] = 0x0aaa0000; regions[12][4] = 0x50550000;
    reinterpret_cast<u8*>(regions[12])[24] = 0x22;
    require(sdk_drum_values(0, &model, raw) && model == expected_model && !std::memcmp(raw, expected, 8));
    const u8 seven[8] = {7, 7, 7, 7, 7, 7, 7, 7};
    require(drum_pack(9, seven, regions[12] + 1));
    reinterpret_cast<u8*>(regions[12])[24] = drum_mark(0x22);
    require(sdk_drum_values(0, &model, raw) && model == expected_model && !std::memcmp(raw, expected, 8));
    require(drum_pack(1, seven, regions[12] + 1));
    require(sdk_drum_values(0, &model, raw) && model == 1 && !std::memcmp(raw, seven, 8));
    require((reinterpret_cast<u8*>(regions[12])[24] >> 4) == 2);   // stock's choke group kept
    shifted_octave = false;

    ownership = 0;
    require(sdk_drum_edit(53, 0, one, 0) == 1234 && fallback == 1);
    require(!sdk_drum_values(0, &model, raw));
    shown(0,1,2,3);
    sdk_synth_press(0); ++presses; shown(0,1,2,3);
    require(clicks == presses);

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

    // An absent slot (slot 1 here) turns nothing: stock gets a turn of no detents.
    engine = 35; page = 0;
    turned_detents = -1;
    sdk_synth_turn(0, 0, 1, 1);
    require(turned_slot == 1 && turned_detents == 0);
    sdk_synth_turn(0, 0, 2, 1);
    require(turned_slot == 2 && turned_detents == -1);
    return 0;
}
