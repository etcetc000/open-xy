#include <cstdio>
#include "stock.hpp"
#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "synth_controls.hpp"
#include "drum_locks.hpp"
#include "drum_lock_store.hpp"
#include <cstdlib>
#include <cstring>

extern "C" {
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
opxy::u32 sdk_synth_scramble(opxy::u32, opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_synth_scramble");
}
namespace {
using namespace opxy;
using namespace opxy::platform;
u32 track, key = 53, engine = 34, page, controller = 0x8400dff8;
u32 publications, dirty, clicks, fallback, last_key, turned_slot;
int ownership = 1;
bool shifted, shifted_octave;
i32 regions[24][32];
void require_at(bool ok, int line) { if (!ok) { std::fprintf(stderr, "line %d\n", line); std::abort(); } }
#define require(ok) require_at(ok, __LINE__)
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
// The send tracks (14, 15) hold no owned effect.
u32 get_engine(u32 selected) { if (selected >= 8) return 0; require(selected == track); return engine; }
// Per-hit locks (drum_lock_store.hpp): whether a step is held, the held
// step's words 36..38 as stock's editor shows them, stock's lock writer, the
// selected track's stored words (its track object at a stand-in address) and
// its live words as 8d5acea8 sets them.
bool held;
i32 held_words[3];
u32 held_step = 5, held_page = 2, written_step, written_index, written_value, writes, redraws;
i32 turn_event[3];
const u32 kObject = 0x10000000;
i32 object[0x3864 / 4 + 58];
i32 live_set[58];
u32 live_sets, sets, loading, steps_asked;
u32 step_held() { return held; }
i32 held_word(u32 project, u32 selected, u32 index) {
    require(project == 0x86bd027c && selected == track && index >= 36 && index < 39);
    return held_words[index - 36];
}
u32 step_of_page() { return held_step; }
u32 page_of(u32 selected) { require(selected == track); return held_page; }
// 8d586f40: stores the halfword in the step's slot; the held step's value then shows it.
void write_lock(u32 selected, u32 step, u32 index, u32 value) {
    require(selected == track && index >= 36 && index < 38 && value < 0x10000);
    written_step = step; written_index = index; written_value = value; ++writes;
    held_words[index - 36] = static_cast<i32>(value << 16);
}
// Stock's stepped value of bucket k of 201 (8d5ad2e8 stores a bucket's centre).
i32 drum_lock_word_pan_test(i32 offset) { const i32 width = 0x7fffffff / 201; return width * (offset + 100) + width / 2; }
const u32 kRedraw = 0x10100000;
u32 redraw_target() { return kRedraw; }
void redraw(u32 target) { require(target == kRedraw); ++redraws; }
u32 track_object(u32 selected) { require(selected == track); return kObject; }
void live(u32 project, u32 selected, u32 index, i32 value) {
    require(project == 0x86bd027c && selected == track && index < 58);
    live_set[index] = value; ++live_sets;
}
u32 set(u32, u32 selected, u32 index, i32 value) { require(selected == track && index < 58); object[0x3864 / 4 + index] = value; ++sets; return 0; }
i32 scrambled(u32, u32) { return 0x1234; }
u32 stock_steps(u32, u32, u32* scale) { ++steps_asked; *scale = 2; return 0xffffffffu; }
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
void shown_at(u32 a, u32 b, u32 c, u32 d, int line) {
    u8 slots[4]; sdk_synth_shown(track, slots);
    require_at(slots[0] == a && slots[1] == b && slots[2] == c && slots[3] == d, line);
}
#define shown(a, b, c, d) shown_at(a, b, c, d, __LINE__)
}

extern "C" {
// Every drum kit's press pages its whole row, as the generated table marks it.
// Engine 37 groups its five models as two and three; engine 38 has 33, so it
// stores its keys in the prefix code.
static const opxy::u8 group_sizes[] = {2, 3};
static const opxy::u8 pad_bits[] = {5, 4};
static const char* const pad_labels[] = {"FILTER", "SRR"};
static bool pad_text(opxy::u32, opxy::i32, char*) { return false; }
static const char* const group_names[] = {"ONE", "TWO"};
const opxy::platform::DrumMachine sdk_drum_machines[] = {
    {reinterpret_cast<const char*>(34), ready, 2, defaults, label, name, count},
    {reinterpret_cast<const char*>(37), ready, 5, defaults, label, name, count, 2, group_sizes, group_names},
    {reinterpret_cast<const char*>(38), ready, 33, defaults, label, name, count},
    // padMix: engine 39 with groups, engine 40 without.
    {reinterpret_cast<const char*>(39), ready, 5, defaults, label, name, count, 2, group_sizes, group_names, 1},
    {reinterpret_cast<const char*>(40), ready, 2, defaults, label, name, count, 0, nullptr, nullptr, 1},
    // padMix with groups and two pad controls: FILTER (5 bits, signed) and SRR (4).
    {reinterpret_cast<const char*>(41), ready, 5, defaults, label, name, count, 2, group_sizes, group_names, 1,
     2, pad_bits, 1, pad_labels, pad_text}
};
const opxy::u32 sdk_drum_machine_count = 6;
const opxy::platform::SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(34), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(37), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(38), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(39), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(40), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(41), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true}
};
const opxy::u32 sdk_synth_control_count = 6;
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
    case 0x8d5b7e34: return reinterpret_cast<Address>(&step_held);
    case 0x8d5ac93c: return reinterpret_cast<Address>(&held_word);
    case 0x8d587708: return reinterpret_cast<Address>(&track_object);
    case 0x8d5acea8: return reinterpret_cast<Address>(&live);
    case 0x8d5acf28: return reinterpret_cast<Address>(&set);
    case 0x8c0b5230: return reinterpret_cast<Address>(&scrambled);
    case 0x8c0b2468: return reinterpret_cast<Address>(&stock_steps);
    case 0x840a0528: return reinterpret_cast<Address>(&loading);
    case 0x8d5b7e4c: return reinterpret_cast<Address>(&step_of_page);
    case 0x8d585fb4: return reinterpret_cast<Address>(&page_of);
    case 0x8d586f40: return reinterpret_cast<Address>(&write_lock);
    case 0x8d54b438: return reinterpret_cast<Address>(&redraw_target);
    case 0x8d54b3b6: return reinterpret_cast<Address>(&redraw);
    case 0x1000: return reinterpret_cast<Address>(turn_event);
    default:
        if (address >= kObject && address < kObject + sizeof(object))
            return reinterpret_cast<Address>(object) + (address - kObject);
        std::abort();
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
    sdk_synth_press(1); ++presses; shown(4,5,6,7);   // a press pages the whole row
    const u32 event[3] = {1000, 3, 2};
    sdk_drum_edit(53, 1, event, 0);   // the second encoder's second row, slot 5
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
    require(sdk_drum_choke_choice(0) == 3);
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
    // A gap: the third encoder's slot 2 is blank and inert; the second row
    // pages in whole, its gap (slot 7) included.
    shifted = false;
    sdk_synth_press(3); ++presses; shown(0,1,2,3);
    sdk_drum_edit(53, 2, event, 0);
    require(publications == 3 && drum_unpack(regions[0] + 1, &model, raw) && raw[2] == 22);
    sdk_synth_press(2); ++presses; shown(4,5,6,7);
    sdk_drum_edit(53, 3, event, 0);   // slot 7, the gap: inert
    require(publications == 3);
    sdk_synth_press(0); ++presses; shown(0,1,2,3);

    // Shift and the first encoder choose the model, as on Drumline; the
    // controls stay.
    const u32 one[3] = {0, 1, 0};
    sdk_drum_edit(53, 4, one, 0);
    require(sdk_drum_values(0, &model, raw) && model == 1 && raw[5] == 28 && publications == 4);
    shown(0,1,2,3);
    sdk_drum_edit(53, 4, event, 0);   // the last model is the limit
    require(publications == 4);
    shifted = false;

    key = 54; shown(0,1,2,3);
    sdk_synth_press(1); ++presses; shown(0,1,2,3);   // the shorter model has no second row: a press stays
    sdk_drum_edit(54, 3, event, 0);   // nor a slot 3: inert
    require(publications == 4);
    require(!std::strcmp(sdk_drum_title(34, 1), "short"));
    key = 53;
    const u32 back[3] = {0, static_cast<u32>(-1), 0};
    shifted = true;
    sdk_drum_edit(53, 4, back, 0);
    shifted = false;
    require(sdk_drum_values(0, &model, raw) && model == 0 && publications == 5);
    sdk_synth_press(1); ++presses; shown(4,5,6,7);
    key = 54; shown(0,1,2,3);   // a key without a second row returns to the first
    key = 53;
    sdk_synth_press(1); ++presses; shown(4,5,6,7);
    sdk_synth_press(1); ++presses; shown(0,1,2,3);

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
        // model leaves it unnamed. A 33-model kit keeps every control at 7
        // bits: a detent is one unit, to 127, and slot 7 keeps its value.
        engine = 38; key = 55; shifted = false;
        shown(0,1,2,3);
        sdk_synth_press(2); ++presses; shown(4,5,6,7);
        sdk_drum_edit(55, 2, up, 0);
        require(sdk_drum_values(0, &m, v) && m == 0 && v[6] == 27 && v[7] == 55 && v[5] == 25);
        const u32 far[3] = {0, 100, 0};
        sdk_drum_edit(55, 2, far, 0);
        require(sdk_drum_values(0, &m, v) && v[6] == 127);
        sdk_drum_edit(55, 2, back5, 0);
        require(sdk_drum_values(0, &m, v) && v[6] == 122);
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        engine = 34; key = 53;
    }

    {
        // padMix: Shift's first row is the model (every model, through the
        // groups), the choke group, the key's stock pan and its level; its
        // second, every key's, the pad fields: the kit's own pad controls on
        // positions 0 and 1, then FX I and FX II. Pan moves as stock's editor
        // does, (event[2] + 1) * event[1] a turn, within -100..100; level one
        // dB a detent, a cut within -30..0. Neither touches the stored words
        // or the mark.
        engine = 39; key = 56; shifted = true;
        u8* pad = reinterpret_cast<u8*>(regions[3]);
        const DrumMachine* kit = sdk_drum_machine(39);
        require(kit && kit->second_shift_row() && !sdk_drum_machine(37)->second_shift_row());
        for (u32 id = 39; id <= 41; ++id) {
            const DrumMachine* m = sdk_drum_machine(id);
            require(m->shift(0, false) == DrumMachine::kShiftModel && m->shift(1, false) == DrumMachine::kShiftChoke &&
                    m->shift(2, false) == DrumMachine::kShiftPan && m->shift(3, false) == DrumMachine::kShiftLevel);
            require(m->shift(2, true) == DrumMachine::kShiftPad2 && m->shift(3, true) == DrumMachine::kShiftPad3);
            require(!std::strcmp(m->pad_label(2), "FX I") && !std::strcmp(m->pad_label(3), "FX II"));
        }
        require(kit->shift(0, true) == DrumMachine::kShiftNone && kit->shift(1, true) == DrumMachine::kShiftNone);
        // Without padMix nothing moves: the third choke (groups) or nothing, the fourth nothing.
        require(sdk_drum_machine(37)->shift(2, true) == DrumMachine::kShiftChoke && sdk_drum_machine(37)->shift(0, false) == DrumMachine::kShiftGroup);
        require(sdk_drum_machine(34)->shift(2, false) == DrumMachine::kShiftNone);
        i32 words[5];
        std::memcpy(words, regions[3], sizeof(words));
        const u8 mark = pad[24];
        require(sdk_drum_pan(0) == 0 && sdk_drum_level(0) == 0);
        u32 before = publications;
        const u32 fast[3] = {0, 3, 2}, slow[3] = {0, 1, 0}, down[3] = {0, static_cast<u32>(-1), 0};
        sdk_drum_edit(56, 6, fast, 0);
        require(static_cast<signed char>(pad[0x1e]) == 9 && sdk_drum_pan(0) == 9 && publications == before + 1);
        require(!std::memcmp(words, regions[3], sizeof(words)) && pad[24] == mark);
        const u32 left[3] = {0, static_cast<u32>(-100), 3};
        sdk_drum_edit(56, 6, left, 0);
        require(sdk_drum_pan(0) == -100 && publications == before + 2);
        sdk_drum_edit(56, 6, left, 0);   // the left end is the limit
        require(publications == before + 2);
        const u32 far[3] = {0, 127, 99};
        sdk_drum_edit(56, 6, far, 0);
        require(sdk_drum_pan(0) == 100);
        const u32 back[3] = {0, static_cast<u32>(-100), 0};
        sdk_drum_edit(56, 6, back, 0);
        require(sdk_drum_pan(0) == 0 && pad[0x1e] == 0);   // 0 is the centre
        before = publications;
        sdk_drum_edit(56, 7, slow, 0);   // 0 dB is the top: no boost
        require(publications == before && pad[0x1d] == 0);
        sdk_drum_edit(56, 7, down, 0);
        require(static_cast<signed char>(pad[0x1d]) == -1 && sdk_drum_level(0) == -1 && publications == before + 1);
        const u32 many[3] = {0, static_cast<u32>(-50), 7};   // one dB a detent, whatever the speed
        sdk_drum_edit(56, 7, many, 0);
        require(sdk_drum_level(0) == -30 && static_cast<signed char>(pad[0x1d]) == -30);
        // A boost stock stored (up to +20 dB) shows and plays as 0 dB; turning
        // up keeps it, turning down cuts from 0 dB.
        pad[0x1d] = 12;
        require(sdk_drum_level(0) == 0);
        before = publications;
        sdk_drum_edit(56, 7, slow, 0);
        require(pad[0x1d] == 12 && publications == before);
        sdk_drum_edit(56, 7, down, 0);
        require(static_cast<signed char>(pad[0x1d]) == -1);
        require(!std::memcmp(words, regions[3], sizeof(words)) && pad[24] == mark);
        // The model scrolls every model, through the groups (5 models, groups
        // of 2 and 3); the choke group is on the second encoder.
        key = 53;
        const u32 up[3] = {0, 1, 0}, ten[3] = {0, 10, 0}, all_back[3] = {0, static_cast<u32>(-10), 0};
        sdk_drum_edit(53, 4, all_back, 0);
        u8 m, v[8];
        require(sdk_drum_values(0, &m, v) && m == 0);
        sdk_drum_edit(53, 4, up, 0); sdk_drum_edit(53, 4, up, 0);
        require(sdk_drum_values(0, &m, v) && m == 2);   // into the second group
        sdk_drum_edit(53, 4, ten, 0);
        require(sdk_drum_values(0, &m, v) && m == 4);   // the last model is the limit
        u8* first = reinterpret_cast<u8*>(regions[0]);
        const u32 choke_before = drum_choke(first[24]);
        sdk_drum_edit(53, 5, choke_before < 3 ? up : down, 0);
        require(drum_choke(first[24]) != choke_before);
        // The second row: every key has one, a short model's too (key 54).
        shifted = false;
        key = 54;
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        u8* other = reinterpret_cast<u8*>(regions[1]);
        i32* other_word = regions[1] + 5;
        before = publications;
        sdk_drum_edit(54, 4, up, 0);   // no pad controls here: positions 0 and 1 turn nothing
        sdk_drum_edit(54, 5, up, 0);
        require(publications == before);
        sdk_drum_edit(54, 6, up, 0);   // FX I
        sdk_drum_edit(54, 7, ten, 0);  // FX II, to its top
        require(publications == before + 2 && drum_marked(other[24]));
        require(((static_cast<u32>(*other_word) >> 25) & 7) == 1 && ((static_cast<u32>(*other_word) >> 28) & 7) == 7);
        i32 values[4];
        sdk_drum_pad_values(0, values);
        require(!values[0] && !values[1] && values[2] == 1 && values[3] == 7);
        sdk_drum_edit(54, 6, back, 0);   // back to 0: TRK, the track's send
        sdk_drum_pad_values(0, values);
        require(!values[2] && other[0x1e] == 0 && other[0x1d] == 0);
        shifted = false;
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        // padMix without groups: the same first row.
        engine = 40; key = 53; shifted = true;
        sdk_drum_edit(53, 6, up, 0);
        sdk_drum_edit(53, 7, down, 0);
        require(sdk_drum_pan(0) == 1 && sdk_drum_level(0) == -1);
        first[0x1e] = first[0x1d] = 0;
        engine = 34; key = 53; shifted = false;
        require(sdk_drum_pan(8) == 0 && sdk_drum_level(8) == 0);   // no instrument track
    }

    {
        // Pad controls: positions 0 and 1 of the second Shift row, packed from
        // bit 16 of the pad word +0x14 (FILTER 16..20, SRR 21..24), then the
        // sends (FX I 25..27, FX II 28..30). An unmarked pad reads neutral
        // whatever stock left there, and its first edit clears bits 16..30;
        // stock's low half and bit 31 stay.
        engine = 41; key = 57; shifted = true;
        const DrumMachine* kit = sdk_drum_machine(41);
        require(kit->shift(0, true) == DrumMachine::kShiftPad && kit->shift(1, true) == DrumMachine::kShiftPad1);
        require(!std::strcmp(kit->pad_label(0), "FILTER") && !std::strcmp(kit->pad_label(1), "SRR"));
        i32* word = regions[4] + 5;
        u8* pad = reinterpret_cast<u8*>(regions[4]);
        require(!drum_marked(pad[24]));
        *word = 0x7a5a1234;   // a value stock left: bit 31 clear, its fields nonzero
        i32 values[4] = {9, 9, 9, 9};
        sdk_drum_pad_values(0, values);
        require(!values[0] && !values[1] && !values[2] && !values[3]);
        shifted = false;
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        const u32 left[3] = {0, static_cast<u32>(-3), 0}, up[3] = {0, 1, 0}, far[3] = {0, 100, 0}, back[3] = {0, static_cast<u32>(-100), 0};
        u32 before = publications;
        sdk_drum_edit(57, 4, left, 0);   // FILTER three steps low-pass
        require(drum_marked(pad[24]) && publications == before + 1);
        require((static_cast<u32>(*word) & kDrumPadMask) == ((32u - 3u) << 16) && (*word & 0xffff) == 0x1234);
        sdk_drum_pad_values(0, values);
        require(values[0] == -3 && !values[1] && !values[2] && !values[3]);
        sdk_drum_edit(57, 4, back, 0);
        sdk_drum_pad_values(0, values);
        require(values[0] == -16);
        sdk_drum_edit(57, 4, far, 0);
        sdk_drum_edit(57, 5, far, 0);
        sdk_drum_edit(57, 6, far, 0);
        sdk_drum_edit(57, 7, up, 0);
        sdk_drum_pad_values(0, values);
        require(values[0] == 15 && values[1] == 15 && values[2] == 7 && values[3] == 1);
        require((static_cast<u32>(*word) >> 16 & 31) == 15 && (static_cast<u32>(*word) >> 21 & 15) == 15 &&
                (static_cast<u32>(*word) >> 25 & 7) == 7 && (static_cast<u32>(*word) >> 28 & 7) == 1);
        before = publications;
        sdk_drum_edit(57, 5, up, 0);     // the last step is the limit
        require(publications == before);
        // A project keeps the whole record; a preset that stores the word's top
        // half (>> 16, restored << 16) keeps every field too.
        const i32 restored = static_cast<i32>((static_cast<u32>(*word) >> 16) << 16);
        for (u32 f = 0; f < 4; ++f) {
            u32 low, bits;
            bool sign;
            require(kit->pad_field(f, low, bits, sign) && drum_field_get(restored, low, bits, sign) == drum_field_get(*word, low, bits, sign));
        }
        // Another kit's edits leave the word alone.
        engine = 37; key = 57;
        const i32 kept = *word;
        sdk_drum_edit(57, 6, up, 0);
        require(*word == kept);   // another engine's track starts on its first row again
        shifted = false;
        engine = 34; key = 53;
    }

    {
        // A Shift press (stock's Shift-press action, through the hook at
        // 8c0b2730) switches a padMix kit's row, both ways, a short model's
        // key too; it never reaches stock's press recorder. Other kits, other
        // pages and other controllers keep stock's.
        engine = 39; key = 54; shifted = true;
        const u32 clicks_before = clicks;
        shown(0,1,2,3);
        require(sdk_drum_shift_press(0, 2)); shown(4,5,6,7);
        require(sdk_drum_shift_press(0, 0)); shown(0,1,2,3);
        require(!sdk_drum_shift_press(1, 0) && !sdk_drum_shift_press(0, 4));   // another track; no such encoder
        page = 1;
        require(!sdk_drum_shift_press(0, 0));
        page = 0;
        controller = 0x1234;
        require(!sdk_drum_shift_press(0, 0));
        controller = 0x8400dff8;
        shown(0,1,2,3);
        engine = 37;   // groups, no padMix: no second Shift row
        require(!sdk_drum_shift_press(0, 0));
        shown(0,1,2,3);
        require(clicks == clicks_before);
        engine = 34; key = 53; shifted = false;
    }

    {
        // Per-hit locks of a padMix kit's Shift fields (drum_locks.hpp). Stock
        // routes Shift's turns to sdk_drum_edit unless a step is held; then
        // here, where they lock the held step's hits: PAN through stock's
        // editor on word 38 (its note-start pan term), the rest packed into 36
        // and 37 through stock's lock writer at the held step.
        engine = 41; key = 57; shifted = true;
        u32 project = 0x86bd027c;
        const u32 event = 0x1000;
        turn_event[0] = 1;
        turned_slot = 99;
        // No step held: a Shift turn that reaches here is stock's fine turn of
        // the engine control the position shows, as before.
        sdk_synth_turn(project, 0, 6, event);
        require(turned_slot == 2);
        held = true;
        // First row: MACHINE and CHOKE lock nothing; PAN goes to stock's editor
        // on 38; LEVEL is written into 36 at the held step (5 + 16 x page 2).
        for (u32 position = 0; position < 2; ++position) {
            turned_slot = 99;
            require(sdk_synth_turn(project, 0, 4 + position, event) == 0 && turned_slot == 99 && !writes);
        }
        sdk_synth_turn(project, 0, 6, event);
        require(turned_slot == 38 && !writes);
        turned_slot = 99;
        turn_event[0] = -3;
        sdk_synth_turn(project, 0, 7, event);
        require(turned_slot == 99 && writes == 1 && redraws == 1 && written_step == 37 && written_index == 36);
        DrumHitLocks shown;
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitLevel] == -3 && !shown.offset[kHitSend1] && sdk_drum_level(0) == -3);
        turn_event[0] = -40;   // clamped at -30
        sdk_synth_turn(project, 0, 7, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitLevel] == -30 && writes == 2);
        sdk_synth_turn(project, 0, 7, event);
        require(writes == 2);   // no change, no write
        // Second row: FILTER and SRR into 37; FX I and FX II into 36 beside LEVEL.
        shifted = false;
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        turn_event[0] = 2;
        sdk_synth_turn(project, 0, 4, event);
        require(writes == 3 && written_index == 37);
        turn_event[0] = -1;
        sdk_synth_turn(project, 0, 5, event);
        require(writes == 4 && written_index == 37);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitPad1] == 2 && shown.offset[kHitPad2] == -1 && shown.offset[kHitLevel] == -30);
        // A send lock starts from what the key plays: key 57 plays FX I at code 3
        // (lock 4), so a detent up locks 4 + 1; FX II from TRK (lock 1).
        u8* pad = reinterpret_cast<u8*>(regions[4]);
        pad[24] = drum_mark(0x20);
        regions[4][5] = static_cast<i32>((3u << 25) | 0x1234u);
        turn_event[0] = 1;
        sdk_synth_turn(project, 0, 6, event);
        require(writes == 5 && written_index == 36);
        sdk_synth_turn(project, 0, 7, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitSend1] == 5 && shown.offset[kHitSend2] == 2 && shown.offset[kHitLevel] == -30);
        turn_event[0] = 20;    // at most 100 (lock 8)
        sdk_synth_turn(project, 0, 7, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitSend2] == 8);
        // The Shift row shows what the step plays: the pad controls with their
        // offsets, a send with its override (code = lock - 1).
        i32 values[4];
        sdk_drum_pad_values(0, values);
        require(values[0] == 2 && values[1] == 0 && values[2] == 4 && values[3] == 7);   // SRR clamps at its 0
        // Unshifted, a held step locks the engine control shown, as before.
        shifted = false;
        sdk_synth_turn(project, 0, 1, event);
        require(turned_slot == 5);
        // A padMix kit without pad controls: FILTER and SRR's positions lock
        // nothing; FX I and FX II still lock.
        engine = 39;   // another engine: its encoders start on the first row
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        const u32 before = writes;
        for (u32 position = 0; position < 2; ++position) {
            turned_slot = 99;
            require(sdk_synth_turn(project, 0, 4 + position, event) == 0 && turned_slot == 99);
        }
        require(writes == before);
        sdk_synth_turn(project, 0, 6, event);
        require(writes == before + 1);
        shifted = false;
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        // A kit without padMix keeps stock's fine-turn lock of the control shown.
        engine = 34; key = 53; shifted = true;
        sdk_synth_turn(project, 0, 6, event);
        require(turned_slot == 2);

        // Stock's step asks PAN's word (38) for its bucket count: one a unit,
        // scale 1. M3's words (16..19) are stock's own again.
        engine = 41;
        u32 scale = 9;
        require(sdk_param_steps(0, 38, &scale) == 201 && scale == 1);
        u32 asked = steps_asked;
        for (u32 index = 16; index < 20; ++index) {
            scale = 9;
            require(sdk_param_steps(0, index, &scale) == 0xffffffffu && steps_asked == ++asked);
        }
        require(sdk_param_steps(0, 36, &scale) == 0xffffffffu);
        engine = 34;
        require(sdk_param_steps(0, 38, &scale) == 0xffffffffu && scale == 2);

        // The codec: each field round-trips through its 16-bit slot, 0 is no lock.
        const DrumMachine& kit = *sdk_drum_machine(41);
        for (u32 field = kHitLevel; field < kHitFields; ++field) {
            i32 low, high;
            drum_lock_range(kit, field, low, high);
            require(low < high && drum_hit_get(0, field) == 0);
            for (i32 lock = low; lock <= high; ++lock) {
                const u32 half = drum_hit_put(0xffffu, field, lock);
                require(drum_hit_get(half, field) == lock);
                for (u32 other = kHitLevel; other < kHitFields; ++other)
                    if (other != field && drum_hit_slot(other).word == drum_hit_slot(field).word)
                        require(drum_hit_get(half, other) == drum_hit_get(0xffffu, other));
            }
        }
        i32 low, high;
        drum_lock_range(kit, kHitPad1, low, high);
        require(low == -31 && high == 31);
        drum_lock_range(kit, kHitPad2, low, high);
        require(low == -15 && high == 15);
        drum_lock_range(*sdk_drum_machine(39), kHitPad1, low, high);
        require(low == 0 && high == 0);

        // PAN with a step held: stock's push (8d51a688) of the key's pan by the
        // step's term; centred, the key's own.
        engine = 41; key = 57;
        pad[0x1e] = 90; pad[0x1d] = static_cast<u8>(-12);
        held_words[2] = kDrumPanTermNeutral;
        require(sdk_drum_pan(0) == 90);
        held_words[2] = 0x7fffffff;                       // the whole way right
        require(sdk_drum_pan(0) == 100);
        held_words[2] = 0x60000000;                       // half way from 90 to the right: 95
        require(sdk_drum_pan(0) == 95);
        held_words[2] = 0x20000000;                       // half of 90's distance from hard left
        require(sdk_drum_pan(0) == -5);
        held_words[2] = drum_lock_word_pan_test(0);
        require(sdk_drum_pan(0) == 90);                   // stock's middle bucket: centre
        require(sdk_drum_level(0) == -30);
        held = false;
        require(sdk_drum_pan(0) == 90 && sdk_drum_level(0) == -12);
        sdk_drum_pad_values(0, values);
        require(values[0] == 0 && values[2] == 3 && values[3] == 0);
        pad[0x1e] = pad[0x1d] = 0;
        pad[24] = 0; regions[4][5] = 0;

        // Saved values: after scramble the per-hit words have no lock, stored
        // and live (stock's scramble leaves 36..41; whatever put one there).
        // Other kits keep stock's scramble.
        object[0x3864 / 4 + 36] = 0x10000; object[0x3864 / 4 + 37] = 0x20000; object[0x3864 / 4 + 38] = 0;
        object[0x3864 / 4 + 16] = 0x1111;
        live_sets = 0;
        sdk_synth_scramble(project, 0, 13, 77);
        require(!object[0x3864 / 4 + 36] && !object[0x3864 / 4 + 37] && object[0x3864 / 4 + 38] == kDrumPanTermNeutral);
        require(!live_set[36] && !live_set[37] && live_set[38] == kDrumPanTermNeutral && live_sets == 3);
        require(object[0x3864 / 4 + 13] == 77 && object[0x3864 / 4 + 16] == 0x1111);   // M3's are the scramble's
        engine = 34;
        object[0x3864 / 4 + 36] = 5;
        sdk_synth_scramble(project, 0, 13, 78);
        require(object[0x3864 / 4 + 36] == 5 && live_sets == 3);
        // Once a frame the selected kit's base is set back if it is not, never
        // while a project loads.
        engine = 41;
        loading = 1;
        sdk_send_refresh();
        require(object[0x3864 / 4 + 36] == 5 && live_sets == 3);
        loading = 0;
        sdk_send_refresh();
        require(!object[0x3864 / 4 + 36] && !live_set[36] && live_sets == 4);
        sdk_send_refresh();
        require(live_sets == 4);   // already neutral: nothing written
        // Project load's check of each pattern's track object: a padMix kit's
        // per-hit words with no lock, M3's words untouched, the alias as before.
        object[0x3864 / 4 + 36] = 9; object[0x3864 / 4 + 37] = 9; object[0x3864 / 4 + 38] = 0;
        object[0x3864 / 4 + 16] = 3;
        require(sdk_drum_load_engine(34, kObject) == 4 && object[0x3864 / 4 + 36] == 9);
        require(sdk_drum_load_engine(99, kObject) == 99 && object[0x3864 / 4 + 36] == 9);
        require(sdk_drum_load_engine(41, kObject) == 4);
        require(!object[0x3864 / 4 + 36] && !object[0x3864 / 4 + 37] && object[0x3864 / 4 + 38] == kDrumPanTermNeutral);
        require(object[0x3864 / 4 + 16] == 3 && object[0x3864 / 4 + 35] == 0 && object[0x3864 / 4 + 39] == 0);
        shifted = false; held = false;
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
