#include <cstdlib>
#include <cstring>
#include <initializer_list>
static void host_barrier() {}
#include <drum_controls_stock.cpp>
#include <keyboard_octaves.cpp>

using namespace opxy::platform;
static void check(bool value) { if (!value) std::abort(); }
static bool ready = true;
static int owned() { return ready; }
static opxy::PadSettings defaults() { return 77; }
static opxy::PadSettings assign(const opxy::SampleAssignment*) { return 77; }
static void control(opxy::PadSettings*, const opxy::DrumControl*) {}
static unsigned pads[24][8], edits[24], last_index, last_note, selected_track_value;
static unsigned stock_turns, region_publications;
static int octave;
static bool shifted, held;
extern "C" {
const SampleMachine sdk_sample_machines[] = {
    {reinterpret_cast<const char*>(35), 7, owned, defaults, assign, control, nullptr, 0, nullptr, nullptr, nullptr, nullptr},
    {reinterpret_cast<const char*>(36), 2, owned, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr},
    {reinterpret_cast<const char*>(37), 7, owned, defaults, assign, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr}
};
const opxy::u32 sdk_sample_machine_count = 3;
opxy::u32 sdk_sample_edit(void* raw, opxy::u32 index, opxy::i32 direction, opxy::u32 engine) {
    check(engine == 35 && (direction == 1 || direction == 0));
    const unsigned pad = (static_cast<unsigned*>(raw) - &pads[0][0]) / 8;
    check(pad < 24);
    ++edits[pad]; last_index = index;
    store_pad_settings(raw, saved_settings(raw).value + 1);
    return 1;
}
}
static unsigned engine = 35, clipboard_type = 5, publishes;
static unsigned source[32], destination[32], clipboard[32];
static bool paste_succeeds = true;
static unsigned get_engine(unsigned track) { check(track < 16); return engine; }
static int get_octave(unsigned track) { check(track == selected_track_value); return octave; }
static unsigned get_shift() { return shifted; }
static unsigned get_held(unsigned key) { check(key >= 10 && key <= 13); return held; }
static unsigned inverse_key(unsigned pad, int value) {
    check(pad < 24 && value == octave);
    const int note = 53 + int(pad) + 12 * value;
    return note < 0 ? 0 : note > 127 ? 127 : unsigned(note);
}
static void* get_region(unsigned track, unsigned note, unsigned*, unsigned nearest) {
    check(track == selected_track_value && !nearest);
    last_note = note;
    return pads[sample_drum_key(note, octave) - 53];
}
static unsigned publish_region(unsigned note, unsigned knob, const unsigned* event, unsigned fine) {
    check(note == last_note && knob == 5 && !event[0] && !event[1] && !event[2] && !fine);
    ++region_publications; return 1;
}
static unsigned stock_multi_turn(unsigned, unsigned, const unsigned*, unsigned) { ++stock_turns; return 123; }
static unsigned copy_pad(unsigned track, unsigned key) {
    check(track == 0 && key == 60);
    std::memcpy(clipboard, source, sizeof(source));
    clipboard_type = 4;
    return 1;
}
static unsigned paste_pad(unsigned track, unsigned key) {
    check(track == 0 && key == 62);
    if (clipboard_type != 4 || !paste_succeeds) return 0;
    std::memcpy(destination, clipboard, sizeof(clipboard));
    reinterpret_cast<unsigned char*>(destination)[26] = key;
    ++publishes;
    return 1;
}
Address opxy::platform::host_stock_address(Address address) {
    if (address == kSelectedTrack) return reinterpret_cast<Address>(&selected_track_value);
    if (address == 0x8d58f65c) return reinterpret_cast<Address>(&get_octave);
    if (address == 0x8d5b89dc) return reinterpret_cast<Address>(&inverse_key);
    if (address == 0x8d51f184) return reinterpret_cast<Address>(&get_shift);
    if (address == 0x8d5b7d7c) return reinterpret_cast<Address>(&get_held);
    if (address == 0x8d58926c) return reinterpret_cast<Address>(&get_region);
    if (address == 0x8d4dccd8) return reinterpret_cast<Address>(&publish_region);
    if (address == 0x8d4d63b0) return reinterpret_cast<Address>(&stock_multi_turn);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&get_engine);
    if (address == 0x8d581f34) return reinterpret_cast<Address>(&copy_pad);
    if (address == 0x8d581f90) return reinterpret_cast<Address>(&paste_pad);
    if (address == 0x8409fa2c) return reinterpret_cast<Address>(&clipboard_type);
    if (address == 0x8409fb1c) return reinterpret_cast<Address>(clipboard);
    std::abort();
}
int main() {
    for (unsigned id : {3u, 4u, 18u, 30u, 33u, 35u, 36u, 37u, 62u}) {
        engine = id;
        check(sdk_sample_pad_key_engine(0) == (id == 35 || id == 37 ? 3 : id));
        ready = false;
        check(sdk_sample_pad_key_engine(0) == id);
        ready = true;
        // Effect IDs may overlap instrument IDs; send tracks stay stock.
        check(sdk_sample_pad_key_engine(8) == id);
    }
    // Unique bytes exercise markers, tuning, play mode, reverse, gain and path.
    for (unsigned i = 0; i < 32; ++i) source[i] = 1000 + i;
    source[pad_record::kFade] = sample_store_settings(123456);
    engine = 35;
    check(sdk_sample_pad_copy(0, 60) == 1);
    for (unsigned id : {35u, 3u, 37u, 35u}) {
        engine = id;
        check(sdk_sample_pad_paste(0, 62) == 1);
        unsigned expected[32]; std::memcpy(expected, source, sizeof(source));
        reinterpret_cast<unsigned char*>(expected)[26] = 62;
        expected[pad_record::kFade] = id == 35 ? sample_store_settings(123456) :
            id == 37 ? sample_store_settings(77) : 0;
        check(std::memcmp(destination, expected, sizeof(expected)) == 0);
        check(std::memcmp(clipboard, source, sizeof(source)) == 0);
    }
    check(publishes == 4);
    // Failure must restore the original clipboard too.
    engine = 3; paste_succeeds = false;
    check(sdk_sample_pad_paste(0, 62) == 0 && publishes == 4);
    check(std::memcmp(clipboard, source, sizeof(source)) == 0);
    paste_succeeds = true;
    source[pad_record::kFade] = 123;
    check(sdk_sample_pad_copy(0, 60) == 1);
    engine = 35;
    check(sdk_sample_pad_paste(0, 62) == 1);
    check(destination[pad_record::kFade] == sample_store_settings(77));
    check(clipboard[pad_record::kFade] == 123);
    engine = 3;
    check(sdk_sample_pad_paste(0, 62) == 1 && destination[pad_record::kFade] == 123);
    clipboard_type = 5;
    check(sdk_sample_pad_paste(0, 62) == 0 && publishes == 6);
    // Multi-edit must edit precisely the selected canonical pads, even in
    // the other keyboard octave, and publish each change. Stock fade/pan
    // must never consume owned machine settings.
    engine = 35;
    const unsigned event[] = {1000, 1, 0};
    const unsigned selection[] = {0, 0, (1u << 0) | (1u << 7) | (1u << 23)};
    for (octave = -4; octave <= 4; ++octave) {
        for (unsigned knob : {0u, 3u, 5u, 6u}) {
            for (unsigned pad : {0u, 7u, 23u}) {
                const unsigned before = edits[pad];
                store_pad_settings(pads[pad], 150000);
                check(sdk_sample_multi_control(53 + pad, knob, event, 0) == 1);
                check(edits[pad] == before + 1 && saved_settings(pads[pad]).value == 150001);
                check(last_index == knob && sample_drum_key(last_note, octave) == 53 + pad);
            }
        }
        shifted = true; held = true;
        sdk_sample_multi_control(60, 5, event, 0);
        check(last_index == 21);
        held = false;
        for (unsigned button : {11u, 12u}) {
            unsigned before[24]; std::memcpy(before, edits, sizeof(edits));
            check(sdk_sample_multi_press(selection, 0, button) == 35);
            check(last_index == button - 10 + 12);
            for (unsigned pad = 0; pad < 24; ++pad)
                check(edits[pad] == before[pad] + ((selection[2] >> pad) & 1));
        }
        shifted = false;
    }
    check(stock_turns == 0 && region_publications == 171);
    check(edits[1] == 0 && edits[12] == 0 && edits[19] == 0);
    check(sdk_sample_multi_control(52, 5, event, 0) == 0);
    check(sdk_sample_multi_control(77, 5, event, 0) == 0);
    check(sdk_sample_multi_control(53, 5, nullptr, 0) == 0);
    engine = 37; // Pad settings without controls: protect its fade word too.
    check(sdk_sample_multi_control(53, 5, event, 0) == 0);
    check(sdk_sample_multi_control(53, 6, event, 0) == 0);
    check(sdk_sample_multi_press(selection, 0, 12) == 37);
    for (unsigned id : {3u, 18u, 36u, 62u}) {
        engine = id;
        check(sdk_sample_multi_control(53, 6, event, 0) == 123);
        check(sdk_sample_multi_press(selection, 0, 12) == id);
    }
    engine = 35; ready = false;
    check(sdk_sample_multi_control(53, 6, event, 0) == 123);
    check(sdk_sample_multi_press(selection, 0, 12) == 35);
    check(stock_turns == 5);
}
