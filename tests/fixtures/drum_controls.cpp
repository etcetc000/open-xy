#include <cstdlib>
#include <cstring>
#include <initializer_list>
static void host_barrier() {}
#include <drum_controls_stock.cpp>

using namespace opxy::platform;
static void check(bool value) { if (!value) std::abort(); }
static bool ready = true;
static int owned() { return ready; }
static opxy::PadSettings defaults() { return 77; }
static opxy::PadSettings assign(const opxy::SampleAssignment*) { return 77; }
static void control(opxy::PadSettings*, const opxy::DrumControl*) {}
static void slice_clock(const opxy::SampleSlice*, opxy::SampleSliceRate*) {}
static unsigned pads[24][8], edits[24], last_index, last_note, selected_track_value;
static unsigned stock_turns, region_publications;
static int octave;
static bool shifted, held;
static unsigned slice_starts, slice_stops, rate_multiplies, factory_calls;
static unsigned start_track, start_key, stop_track, stop_key;
static int project_transpose = 5;
static bool remap_notes;
static bool preview_seen;
static SamplePreviewRange captured_range;
static unsigned audition_context;
static bool deliver_packets = true;
static unsigned char last_packet[256];
static unsigned packet_size, extended_packets, plain_packets;
extern "C" {
opxy::u32 sdk_sample_alias(opxy::u32) __asm__("sdk_sample_alias");
const SampleMachine sdk_sample_machines[] = {
    {reinterpret_cast<const char*>(35), 7, owned, defaults, assign, control, nullptr, 0, nullptr, nullptr, nullptr, nullptr, slice_clock},
    {reinterpret_cast<const char*>(36), 2, owned, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr},
    {reinterpret_cast<const char*>(37), 7, owned, defaults, assign, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, slice_clock}
};
const opxy::u32 sdk_sample_machine_count = 3;
void sdk_sample_slice_config(SamplePreviewRange* range, unsigned id, unsigned track, const void*) {
    check((id == 35 || id == 37) && track == 0);
    range->processed = 1; range->bpm_x10 = 500; range->settings = 0x12345678;
    range->level = 0x08000000; range->pitch_cents = -37; range->track_transpose = -12;
    for (unsigned i = 0; i < 8; ++i) range->parameters[i] = i & 1 ? int(i * 17000) : -int(i * 19000);
}
unsigned sdk_sample_slice_clock_rate(unsigned id, const SamplePreviewRange* range, unsigned rate) {
    check((id == 35 || id == 37) && range->bpm_x10 == 500 && range->processed == 1);
    return rate;
}
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
static unsigned get_engine(unsigned track) { check(track < 8); return engine; }
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
static unsigned queue_packet(unsigned context, unsigned kind, const void* raw, unsigned size,
                             unsigned flags, unsigned destination, unsigned source_role) {
    check(context == 0x841460b4 && size <= 249 && flags == 0 && destination == 255 && source_role == 255);
    std::memset(last_packet, 0, sizeof(last_packet));
    last_packet[2] = kind; last_packet[3] = size;
    std::memcpy(last_packet + 4, raw, size); packet_size = size + 4;
    if (size == kSlicePayload) ++extended_packets; else ++plain_packets;
    if (deliver_packets) sdk_sample_slice_receive(&audition_context, last_packet);
    return 103;
}
static unsigned plain_note(unsigned track, unsigned key, unsigned packed, unsigned manual, unsigned fifth) {
    unsigned char bytes[8] = {static_cast<unsigned char>(track), static_cast<unsigned char>(key)};
    put_word(bytes + 2, packed); bytes[6] = manual; bytes[7] = fifth;
    return queue_packet(0x841460b4, 0x98, bytes, 8, 0, 255, 255);
}
static unsigned slice_start(void* context, unsigned track, unsigned key, unsigned velocity, const void* extra) {
    check(context == &audition_context && track == 0 && key == 60 && velocity == 127 && !extra);
    unsigned char routed_track = track, routed_key = key;
    sdk_sample_slice_note_route(context, &routed_track, &routed_key, 0);
    start_track = routed_track;
    start_key = sdk_sample_slice_note_key(routed_track, routed_key);
    check(sdk_sample_slice_send(start_track, start_key, 0x1020307f, 1, 0) == 103);
    // Even inside the audition, a different note/track keeps stock routing.
    check(sdk_sample_slice_note_key(1, 62) == unsigned(62 + project_transpose));
    ++slice_starts; return 101;
}
static unsigned slice_stop(void* context, unsigned track, unsigned key, unsigned fourth) {
    check(context == &audition_context && track == 0 && key == 60 && !fourth);
    unsigned char routed_track = track, routed_key = key;
    sdk_sample_slice_note_route(context, &routed_track, &routed_key, 0);
    stop_track = routed_track;
    stop_key = sdk_sample_slice_note_key(routed_track, routed_key);
    const unsigned char bytes[3] = {static_cast<unsigned char>(stop_track), static_cast<unsigned char>(stop_key), 1};
    check(queue_packet(0x841460b4, 0x99, bytes, 3, 0, 255, 255) == 103);
    ++slice_stops; return 102;
}
static unsigned note_key(unsigned track, unsigned key) {
    check(track < 8 && key < 128);
    return unsigned(int(key) + project_transpose);
}
static unsigned note_route(void* context, unsigned char* track, unsigned char* key, unsigned flag) {
    check(context == &audition_context && track && key && !flag);
    if (remap_notes) { *track = 1; *key = 61; }
    return 55;
}
static unsigned multiply_rate(unsigned rate, unsigned pitch) {
    check(rate == 0x472c4400u && pitch == 0x40000000u);
    ++rate_multiplies; return 0x47ac4400u;
}
static unsigned create_voice(unsigned id, void** output, unsigned index, unsigned track) {
    check(index == 23);
    ++factory_calls;
    const SamplePreviewRange* range = sdk_sample_slice_preview(id, track);
    preview_seen = range != nullptr;
    if (range) captured_range = *range;
    check(sdk_sample_slice_preview(id + 1, track) == nullptr);
    check(sdk_sample_slice_preview(id, track + 1) == nullptr);
    *output = &audition_context;
    return 1;
}
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
    if (address == 0x11a00ec0) return reinterpret_cast<Address>(&queue_packet);
    if (address == 0x8d4dc490) return reinterpret_cast<Address>(&plain_note);
    if (address >= 0x11b01d48 && address < 0x11b01d48 + 8 * 4) return reinterpret_cast<Address>(&engine);
    if (address == 0x8d594278) return reinterpret_cast<Address>(&slice_start);
    if (address == 0x8d5941ac) return reinterpret_cast<Address>(&slice_stop);
    if (address == 0x8d58c418) return reinterpret_cast<Address>(&note_key);
    if (address == 0x8d5940c6) return reinterpret_cast<Address>(&note_route);
    if (address == 0x04021ac0) return reinterpret_cast<Address>(&multiply_rate);
    if (address == 0x8d506e92) return reinterpret_cast<Address>(&create_voice);
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
    // The preset writer borrows each page's corresponding stock sample
    // classification. Every stock or conflicting ID must keep
    // its original serialization route, including stock drumline (4).
    for (bool registered : {false, true}) {
        ready = registered;
        for (unsigned id = 0; id < 256; ++id) {
            const bool sample = registered && (id == 35 || id == 36 || id == 37);
            check(sdk_sample_alias(id) == (sample ? id == 36 ? 2u : 30u : id));
        }
    }
    ready = true;
    for (unsigned id : {3u, 4u, 18u, 30u, 33u, 35u, 36u, 37u, 62u}) {
        engine = id;
        check(sdk_sample_pad_key_engine(0) == (id == 35 || id == 37 ? 3 : id));
        ready = false;
        check(sdk_sample_pad_key_engine(0) == id);
        ready = true;
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

    // Tap-slicer playback must retain the original source range even when
    // the first cuts have already replaced the source pad before allocation.
    ready = true; engine = 35; octave = 0;
    const unsigned pad = sample_drum_key(60, 0) - 53;
    pads[pad][0] = 441000; pads[pad][1] = 100; pads[pad][2] = 440000;
    unsigned before[8]; std::memcpy(before, pads[pad], sizeof(before));
    deliver_packets = false;
    check(sdk_sample_slice_start(&audition_context, 0, 60, 127, nullptr) == 101);
    check(start_track == 0 && start_key == 60);
    // Scope ends when the GUI call returns, even while the slicer stays open.
    check(sdk_sample_slice_note_key(0, 60) == 65);
    check(std::memcmp(before, pads[pad], sizeof(before)) == 0);
    check(sdk_sample_slice_rate(0x472c4400u, 0x40000000u) == 0x472c4400u);
    pads[pad][1] = 9999; pads[pad][2] = 12345;
    // +0x94 is naturally aligned on the target; offset the host descriptor
    // so its void** is also aligned on a 64-bit fixture host.
    alignas(void*) unsigned char storage[0xb0] = {};
    unsigned char* parent = storage + (sizeof(void*) == 8 ? 4 : 0);
    void** output = reinterpret_cast<void**>(parent + 0x94);
    parent[0x50] = start_key; parent[0x55] = start_track; parent[0x56] = 1;
    // Separate firmware roles do not share the GUI snapshot. Only delivery
    // of the serialized stock message may make the audio audition eligible.
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && !preview_seen);
    check(packet_size == kSlicePayload + 4 && extended_packets == 1);
    check(last_packet[4] == 0 && last_packet[5] == 60 && get_word(last_packet + 6) == 0x1020307f);
    check(last_packet[10] == 1 && last_packet[11] == 0);
    sdk_sample_slice_receive(&audition_context, last_packet);
    deliver_packets = true;
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && preview_seen);
    check(*output == &audition_context);
    check(captured_range.begin == 100 && captured_range.end == 440000 && captured_range.frames == 441000);
    check(captured_range.processed == 1 && captured_range.bpm_x10 == 500 && captured_range.settings == 0x12345678);
    check(captured_range.level == 0x08000000 && captured_range.pitch_cents == -37 && captured_range.track_transpose == -12);
    for (unsigned i = 0; i < 8; ++i)
        check(captured_range.parameters[i] == (i & 1 ? int(i * 17000) : -int(i * 19000)));
    check(sdk_sample_slice_preview(35, 0) == nullptr);
    parent[0x56] = 0; // Sequencer note on the same pad keeps authored DSP.
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && !preview_seen);
    parent[0x56] = 1; parent[0x50] = 61;
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && !preview_seen);
    parent[0x50] = 60; parent[0x55] = 1;
    check(sdk_sample_create_voice(35, output, 23, 1) == 1 && !preview_seen);
    parent[0x55] = 0;
    check(sdk_sample_create_voice(37, output, 23, 0) == 1 && !preview_seen);
    check(sdk_sample_slice_stop(&audition_context, 0, 60, 0) == 102);
    check(stop_track == start_track && stop_key == start_key);
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && preview_seen);
    check(sdk_sample_slice_rate(0x472c4400u, 0x40000000u) == 0x47ac4400u);
    // Stock, melodic sample pages and conflicting ownership retain stock
    // audition and timestamp conversion, without any saved-pad mutation.
    for (unsigned id : {3u, 36u, 35u}) {
        engine = id; ready = id != 35;
        check(sdk_sample_slice_start(&audition_context, 0, 60, 127, nullptr) == 101);
        check(start_track == 0 && start_key == 65);
        check(sdk_sample_create_voice(id, output, 23, 0) == 1 && !preview_seen);
        check(sdk_sample_slice_rate(0x472c4400u, 0x40000000u) == 0x47ac4400u);
        check(sdk_sample_slice_stop(&audition_context, 0, 60, 0) == 102);
        check(stop_track == 0 && stop_key == 65);
    }
    check(slice_starts == 4 && slice_stops == 4 && rate_multiplies == 4 && factory_calls == 10);

    // Project transpose and special keyboard routing must never substitute a
    // different sample for the source whose timestamps the slicer records.
    for (int transpose : {-7, 0, 5}) for (bool remap : {false, true}) {
        project_transpose = transpose; remap_notes = remap;
        for (unsigned id : {35u, 37u, 3u, 36u}) {
            engine = id; ready = true;
            const bool owned_page = id == 35 || id == 37;
            check(sdk_sample_slice_start(&audition_context, 0, 60, 127, nullptr) == 101);
            const unsigned expected_track = !owned_page && remap ? 1 : 0;
            const unsigned expected_key = owned_page ? 60 : unsigned((remap ? 61 : 60) + transpose);
            check(start_track == expected_track && start_key == expected_key);
            parent[0x50] = start_key; parent[0x55] = start_track; parent[0x56] = 1;
            check(sdk_sample_create_voice(id, output, 23, start_track) == 1 && preview_seen == owned_page);
            check(sdk_sample_slice_stop(&audition_context, 0, 60, 0) == 102);
            check(stop_track == expected_track && stop_key == expected_key);
            check(sdk_sample_slice_note_key(0, 60) == unsigned(60 + transpose));
            unsigned char route_track = 0, route_key = 60;
            check(sdk_sample_slice_note_route(&audition_context, &route_track, &route_key, 0) == 55);
            check(route_track == (remap ? 1 : 0) && route_key == (remap ? 61 : 60));
        }
    }

    // Stock can still create a pending machine after note-off. Both GUI stop
    // and delivered note-off must preserve that machine's frozen source;
    // the next matching ordinary manual note-on invalidates eligibility.
    engine = 35; ready = true; project_transpose = 0; remap_notes = false;
    check(sdk_sample_slice_start(&audition_context, 0, 60, 127, nullptr) == 101);
    unsigned char valid_packet[256]; std::memcpy(valid_packet, last_packet, sizeof(valid_packet));
    parent[0x50] = 60; parent[0x55] = 0; parent[0x56] = 1;
    deliver_packets = false;
    check(sdk_sample_slice_stop(&audition_context, 0, 60, 0) == 102);
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && preview_seen);
    sdk_sample_slice_receive(&audition_context, last_packet);
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && preview_seen);
    check(captured_range.begin == get_word(valid_packet + 28) && captured_range.end == get_word(valid_packet + 32));
    check(captured_range.bpm_x10 == 500 && captured_range.settings == 0x12345678 && captured_range.pitch_cents == -37);
    deliver_packets = true;
    plain_note(0, 60, 127, 0, 0); // Sequencer provenance does not cancel manual audition.
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && preview_seen);
    plain_note(1, 60, 127, 1, 0); // Another track cannot cancel it.
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && preview_seen);
    plain_note(0, 60, 127, 1, 0);
    check(sdk_sample_create_voice(35, output, 23, 0) == 1 && !preview_seen);

    // A different source must not replace an earlier stopped source's
    // deferred factory context. Exercise both dimensions of the slot key.
    for (unsigned variant = 0; variant < 3; ++variant) {
        const unsigned second_track = variant == 1 ? 0 : 1;
        const unsigned second_key = variant == 0 ? 60 : 61;
        unsigned char second_packet[256]; std::memcpy(second_packet, valid_packet, sizeof(second_packet));
        second_packet[4] = second_track; second_packet[5] = second_key;
        put_word(second_packet + 20, second_track); put_word(second_packet + 24, second_key);
        put_word(second_packet + 28, 20000); put_word(second_packet + 32, 40000);
        sdk_sample_slice_receive(&audition_context, valid_packet);
        const unsigned char first_off[] = {0, 0, 0x99, 3, 0, 60, 1};
        sdk_sample_slice_receive(&audition_context, first_off);
        sdk_sample_slice_receive(&audition_context, second_packet);
        const unsigned char second_off[] = {0, 0, 0x99, 3,
            static_cast<unsigned char>(second_track), static_cast<unsigned char>(second_key), 1};
        sdk_sample_slice_receive(&audition_context, second_off);
        parent[0x50] = 60; parent[0x55] = 0;
        check(sdk_sample_create_voice(35, output, 23, 0) == 1 && preview_seen);
        check(captured_range.begin == get_word(valid_packet + 28) && captured_range.end == get_word(valid_packet + 32));
        parent[0x50] = second_key; parent[0x55] = second_track;
        check(sdk_sample_create_voice(35, output, 23, second_track) == 1 && preview_seen);
        check(captured_range.begin == 20000 && captured_range.end == 40000);
        plain_note(0, 60, 127, 1, 0);
        check(sdk_sample_create_voice(35, output, 23, second_track) == 1 && preview_seen);
        plain_note(second_track, second_key, 127, 1, 0);
        check(sdk_sample_create_voice(35, output, 23, second_track) == 1 && !preview_seen);
        parent[0x50] = 60; parent[0x55] = 0;
        check(sdk_sample_create_voice(35, output, 23, 0) == 1 && !preview_seen);
    }

    // Reject malformed or unowned extensions without reading an incomplete
    // tail or leaving the preceding matching audition active.
    for (unsigned bad = 0; bad < 10; ++bad) {
        sdk_sample_slice_receive(&audition_context, valid_packet);
        unsigned char broken[256]; std::memcpy(broken, valid_packet, sizeof(broken));
        switch (bad) {
        case 0: broken[3] = 8; break;
        case 1: broken[12] ^= 1; break;
        case 2: put_word(broken + 16, 37); break; // Different actual track engine.
        case 3: put_word(broken + 20, 1); break; // Duplicate track mismatch.
        case 4: put_word(broken + 24, 61); break; // Duplicate key mismatch.
        case 5: put_word(broken + 28, 12345); put_word(broken + 32, 12345); break;
        case 6: put_word(broken + 32, 441001); break;
        case 7: put_word(broken + 40, 2); break;
        case 8: ready = false; break;
        case 9: put_word(broken + 16, 36); break;
        }
        sdk_sample_slice_receive(&audition_context, broken);
        check(sdk_sample_create_voice(35, output, 23, 0) == 1 && !preview_seen);
        ready = true;
    }
    check(plain_packets > 0 && extended_packets > 0);
}
