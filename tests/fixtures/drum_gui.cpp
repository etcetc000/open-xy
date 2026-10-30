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
u32 track, key = 53, engine = 34, page, controller = 0x8400dff8;
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
u32 turn(u32, u32 selected, u32 slot, u32) { require(selected == track); turned_slot = slot; return slot; }
const char* title(u32, u32) { return "stock"; }
u32 text(u32, const char*) { return 0; }
void shown(u32 a, u32 b, u32 c, u32 d) {
    u8 slots[4]; sdk_synth_shown(track, slots);
    require(slots[0] == a && slots[1] == b && slots[2] == c && slots[3] == d);
}
}

extern "C" {
// Engine 37 groups its five models as two and three; engine 38 has 33, so it
// stores slots 6 and 7 at 64 steps, and its press pages the whole row.
static const opxy::u8 group_sizes[] = {2, 3};
static const char* const group_names[] = {"ONE", "TWO"};
const opxy::platform::DrumMachine sdk_drum_machines[] = {
    {reinterpret_cast<const char*>(34), ready, 2, defaults, label, name, count},
    {reinterpret_cast<const char*>(37), ready, 5, defaults, label, name, count, 2, group_sizes, group_names},
    {reinterpret_cast<const char*>(38), ready, 33, defaults, label, name, count}
};
const opxy::u32 sdk_drum_machine_count = 3;
const opxy::platform::SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(34), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, false},
    {reinterpret_cast<const char*>(37), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, false},
    {reinterpret_cast<const char*>(38), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true}
};
const opxy::u32 sdk_synth_control_count = 3;
}

opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case kSelectedTrack: return reinterpret_cast<Address>(&track);
    case 0x080238bc: return reinterpret_cast<Address>(&page);
    case 0x8d58781c: return reinterpret_cast<Address>(&get_engine);
    case 0x8d4d6c98: return reinterpret_cast<Address>(&get_key);
    case 0x8d4d6f14: return reinterpret_cast<Address>(&get_track);
    case 0x8d51f184: return reinterpret_cast<Address>(&get_shift);
    case 0x8c0944b0: return reinterpret_cast<Address>(&top);
    case 0x8d51f1fc: return reinterpret_cast<Address>(&press);
    case 0x8d58926c: return reinterpret_cast<Address>(&region);
    case 0x8d589210: return reinterpret_cast<Address>(&canonical);
    case 0x8d585ee8: return reinterpret_cast<Address>(&changed);
    case 0x8d4d5f18: return reinterpret_cast<Address>(&publish);
    case 0x8d4d5f60: return reinterpret_cast<Address>(&stock_edit);
    case 0x8d5ad5bc: return reinterpret_cast<Address>(&turn);
    case 0x8d506c4c: return reinterpret_cast<Address>(&title);
    case 0x8d6011f0: return reinterpret_cast<Address>(&text);
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
    require(sdk_drum_selected_pad(0) == 65);   // the screen's pad is the audio's
    require(sdk_drum_pad(0, 53) == 65 && sdk_drum_pad(0, 54) == 54);   // a played note's, the same way
    require(!sdk_drum_pad(0, 30) && !sdk_drum_pad(0, 128) && !sdk_drum_pad(8, 53));
    shifted_octave = false;
    require(sdk_drum_selected_pad(0) == 53);

    {
        // Model groups: Shift's first encoder chooses the group, keeping the
        // model's place in it; the second the model in the group; the third
        // the choke group; the fourth nothing.
        engine = 37; key = 54; shifted = true;
        const u32 up[3] = {0, 1, 0}, many[3] = {0, 3, 0}, back5[3] = {0, static_cast<u32>(-5), 0};
        u8 m = 99, v[8];
        require(sdk_drum_values(0, &m, v) && m == 1);   // group ONE, its second
        sdk_drum_edit(54, 4, up, 0);
        require(sdk_drum_values(0, &m, v) && m == 3);   // group TWO, its second
        const u32 before = publications;
        sdk_drum_edit(54, 4, up, 0);   // TWO is the last group
        require(publications == before && sdk_drum_values(0, &m, v) && m == 3);
        sdk_drum_edit(54, 5, many, 0);
        require(sdk_drum_values(0, &m, v) && m == 4);   // the group's last model is the limit
        sdk_drum_edit(54, 5, back5, 0);
        require(sdk_drum_values(0, &m, v) && m == 2);   // and its first
        sdk_drum_edit(54, 4, back5, 0);
        require(sdk_drum_values(0, &m, v) && m == 0);   // ONE, its first
        const u8* pad = reinterpret_cast<const u8*>(regions[1]);
        require(drum_choke(pad[24]) == 0);
        sdk_drum_edit(54, 6, up, 0);
        require(drum_choke(pad[24]) == 1 && !std::strcmp(sdk_drum_choke(0), "1"));
        const u32 still = publications;
        sdk_drum_edit(54, 7, up, 0);
        require(publications == still);

        // A paged kit: a press moves the whole row, slot 7 too though this
        // model leaves it unnamed. 64 steps on slots 6 and 7: a detent moves
        // a step, to 127 at the top, and the other slot keeps its nearest step.
        engine = 38; key = 55; shifted = false;
        shown(0,1,2,3);
        sdk_synth_press(2); ++presses; shown(4,5,6,7);
        sdk_drum_edit(55, 2, up, 0);
        require(sdk_drum_values(0, &m, v) && m == 0 && v[6] == 28 && v[7] == 56 && v[5] == 25);
        const u32 far[3] = {0, 100, 0};
        sdk_drum_edit(55, 2, far, 0);
        require(sdk_drum_values(0, &m, v) && v[6] == 127);
        sdk_drum_edit(55, 2, back5, 0);
        require(sdk_drum_values(0, &m, v) && v[6] == 116);   // step 63 less five
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        engine = 34; key = 53;
    }

    ownership = 0;
    require(sdk_drum_edit(53, 0, one, 0) == 1234 && fallback == 1);
    require(!sdk_drum_values(0, &model, raw));
    require(!sdk_drum_selected_pad(0));
    shown(0,1,2,3);
    sdk_synth_press(0); ++presses; shown(0,1,2,3);
    require(clicks == presses);
    return 0;
}
