#include <sample_callbacks.cpp>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
// The probe's engine ID: on the device the linker supplies the symbol's value;
// here it is the address of a definition, the same on every use.
extern "C" { const char probe_engine_id[1] __asm__("probe_engine_id") = {}; }
static const unsigned kProbe = static_cast<unsigned>(reinterpret_cast<opxy::platform::Address>(probe_engine_id));
// A second machine, on page 2 (the sampler's page).
extern "C" { const char sampler_engine_id[1] __asm__("sampler_engine_id") = {}; }
static const unsigned kSampler = static_cast<unsigned>(reinterpret_cast<opxy::platform::Address>(sampler_engine_id));
extern "C" unsigned sdk_sample_alias(unsigned) __asm__("sdk_sample_alias");
static unsigned selected = kProbe, edits;
extern "C" unsigned sdk_sample_note_route(unsigned) __asm__("sdk_sample_note_route");
static bool owned = true;
static opxy::DrumControl received;
extern "C" unsigned sdk_sample_selected_engine() { return selected; }
extern "C" int probe_ready() { return owned; }
extern "C" int sampler_ready() { return owned; }
// The probe also declares analysis; its callbacks are not exercised here.
extern "C" unsigned probe_analysis_words(const opxy::SampleView*) { return 0; }
extern "C" unsigned probe_prepare_begin(const opxy::SampleView*, unsigned*, unsigned) { return 0; }
extern "C" unsigned probe_prepare_step(unsigned, unsigned*) { return 0; }
extern "C" unsigned probe_prepared(opxy::SampleAnalysis*) { return 0; }
extern "C" void probe_slice_clock(const opxy::SampleSlice*, opxy::SampleSliceRate*) {}
static opxy::SampleAssignment assigned;
extern "C" opxy::PadSettings probe_sample_defaults() { return 77; }
extern "C" opxy::PadSettings probe_sample_assign(const opxy::SampleAssignment* input) {
    assigned = *input; return opxy::PadSettings(input->frames);
}
extern "C" void probe_drum_control(opxy::PadSettings* value, const opxy::DrumControl* event) {
    ++edits; received = *event; ++*value;
}
static const opxy::i32* expected_parameters;
extern "C" void probe_drum_panel(opxy::PadSettings value, const opxy::SampleRegion* saved,
                                 const opxy::i32* parameters, opxy::DrumPanel* frame) {
    if (parameters != expected_parameters || !saved) std::abort();
    frame->pitch_cents = int(value) - 1200; frame->repeat = value & 1;
    frame->reverse = !saved->reverse; frame->level = 0x7fffffff;
    std::memcpy(frame->shifted[0], "first", 6); std::memcpy(frame->shifted[1], "second", 7);
}
static void check(bool value) { if (!value) std::abort(); }
static opxy::u16 project[2] = {1230, 0xffff};
static bool project_present = true;
static opxy::u32 audio_tempo = 0x42f00000u; // Cached audio tempo: 120 BPM.
static const volatile opxy::u16* project_data() { return project_present ? project : nullptr; }
static unsigned assignment_engine(unsigned track) { check(track == 0); return kProbe; }
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d584210) return reinterpret_cast<Address>(&project_data);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&assignment_engine);
    if (address == kAudioBpm) return reinterpret_cast<Address>(&audio_tempo);
    std::abort();
}
int main() {
    check(sdk_sample_machine(kProbe)->slice_clock == &probe_slice_clock);
    check(sdk_sample_machine(kSampler)->slice_clock == nullptr);
    // A GUI tempo edit must reach assignment even when the audio clock has
    // not caught up. Playback must continue reading the audio clock.
    check(opxy::platform::audio_bpm_x10() == 1200);
    for (bool recorded : {false, true}) {
        for (unsigned tempo : {200u, 1230u, 1235u, 3000u, 0u, 199u, 3001u, 65535u}) {
            project[0] = static_cast<opxy::u16>(tempo);
            sdk_sample_assignment(0, 1234, 44100, nullptr,
                                  opxy::platform::sample_store_settings(77), recorded);
            check(assigned.bpm_x10 == (tempo >= 200 && tempo <= 3000 ? tempo : 0));
            check(assigned.recorded == recorded && assigned.replacing && assigned.previous == 77);
            check(opxy::platform::audio_bpm_x10() == 1200);
        }
    }
    project_present = false;
    sdk_sample_assignment(0, 1234, 44100, nullptr, 0, true);
    check(assigned.bpm_x10 == 0 && assigned.recorded && !assigned.replacing);
    project_present = true;
    const opxy::SampleAssignment replacement{"095_loop.wav", 12, 1234, 48000, 1200, true, 0xabcd, false};
    check(sdk_sample_machine(kProbe)->assign(&replacement) == 1234 && assigned.replacing &&
          assigned.previous == 0xabcd && assigned.filename == replacement.filename &&
          assigned.filename_bytes == 12 && assigned.rate == 48000 && !assigned.recorded);
    check(sdk_sample_machine(kProbe)->defaults() == 77);
    // The probe's drum page skips stock's nearest-region test; other engines stay stock's.
    check(sdk_sample_note_route(kProbe) == 2 && sdk_sample_note_route(18) == 0);
    // The page-2 engine takes the sampler's payload and route; the drum page the multisampler's.
    check(sdk_sample_note_route(kSampler) == 3 && sdk_sample_alias(kSampler) == 2);
    check(sdk_sample_alias(kProbe) == 30 && sdk_sample_alias(18) == 18);
    unsigned raw[8] = {1000, 0, 1000, 0, 1000, 0, 60, 0};
    auto* bytes = reinterpret_cast<unsigned char*>(raw);
    // An unassigned, zero-initialized pad has neutral tuning. Painting must
    // not invent +24 or write a default into the stored record.
    unsigned empty[8] = {}, shadow[8];
    empty[5] = opxy::platform::sample_store_settings(1200);
    unsigned original_empty[8]; std::memcpy(original_empty, empty, sizeof(empty));
    auto* empty_panel = sdk_sample_drum_view(empty, shadow, nullptr);
    check(empty_panel && empty_panel->pitch_cents == 0);
    check(reinterpret_cast<unsigned char*>(shadow)[24] == 60);
    check(std::memcmp(empty, original_empty, sizeof(empty)) == 0);
    check(sdk_sample_edit(empty, 0, 1, kProbe) == 1);
    empty_panel = sdk_sample_drum_view(empty, shadow, nullptr);
    check(empty_panel && empty_panel->pitch_cents == 10);
    // A root saved by another engine shows the octave the voice plays, not +24.
    unsigned low_root[8]; std::memcpy(low_root, original_empty, sizeof(low_root));
    reinterpret_cast<unsigned char*>(low_root)[24] = 36;
    empty_panel = sdk_sample_drum_view(low_root, shadow, nullptr);
    check(empty_panel && empty_panel->pitch_cents == 1200);
    // The panel's level reaches stock as rounded float bits.
    check(opxy::platform::q31_float_bits(0) == 0 && opxy::platform::q31_float_bits(1) == 0x30000000u &&
          opxy::platform::q31_float_bits(0x20000000) == 0x3e800000u &&
          opxy::platform::q31_float_bits(0x40000040) == 0x3f000001u &&
          opxy::platform::q31_float_bits(0x7fffffff) == 0x3f800000u);
    // The painted copy plays in the panel's direction (this probe inverts the
    // saved one); the stored record keeps its own.
    check((reinterpret_cast<unsigned char*>(shadow)[31] & 1) == 1 && (reinterpret_cast<unsigned char*>(low_root)[31] & 1) == 0);
    reinterpret_cast<unsigned char*>(low_root)[31] |= 1;
    empty_panel = sdk_sample_drum_view(low_root, shadow, nullptr);
    check(empty_panel && !empty_panel->reverse && (reinterpret_cast<unsigned char*>(shadow)[31] & 1) == 0);
    bytes[27] = 192; bytes[31] = 1;
    for (unsigned index = 0; index < 32; ++index) {
        if ((index & 15) == 0 || (index & 15) == 8) continue; // SDK-owned pad pitch/reset.
        raw[5] = opxy::platform::sample_store_settings(123);
        unsigned before[8]; std::memcpy(before, raw, sizeof(raw));
        check(sdk_sample_edit(raw, index, -4, kProbe) == 1);
        check(opxy::platform::sample_settings(raw[5]).value == 124);
        // A pad without saved settings starts from the machine's defaults.
        raw[5] = 0;
        check(sdk_sample_edit(raw, index, -4, kProbe) == 1 && opxy::platform::sample_settings(raw[5]).value == 78);
        raw[5] = opxy::platform::sample_store_settings(124);
        check(received.knob == (index & 3) && received.shifted == bool(index & 4) &&
              received.pressed == bool(index & 8) && received.held == bool(index & 16) && received.direction == -4);
        before[5] = raw[5]; check(std::memcmp(before, raw, sizeof(raw)) == 0);
    }
    // Sustained pitch edits may change only stock root/fine bytes.
    const unsigned pitch_edits = edits;
    raw[5] = opxy::platform::sample_store_settings(0xabcd);
    for (int direction : {-1, 1}) {
        opxy::platform::sample_gui_pitch(raw, 0);
        for (int step = 1; step <= 140; ++step) {
            unsigned before[8]; std::memcpy(before, raw, sizeof(raw));
            check(sdk_sample_edit(raw, 0, direction * 7, kProbe) == 1);
            check(opxy::platform::sample_gui_region(raw).pitch_cents == direction * (step > 120 ? 1200 : step * 10));
            auto* old_bytes = reinterpret_cast<unsigned char*>(before);
            old_bytes[24] = bytes[24]; old_bytes[28] = bytes[28];
            check(std::memcmp(before, raw, sizeof(raw)) == 0 && edits == pitch_edits);
        }
        check(sdk_sample_edit(raw, 8, 0, kProbe) == 1);
        check(opxy::platform::sample_gui_region(raw).pitch_cents == 0);
    }
    // Active-track edits remain valid when the renderer still caches a stock
    // track. Conversely its cached custom track cannot authorize a stock edit.
    selected = 3;
    check(sdk_sample_edit(raw, 0, 1, kProbe) == 1);
    opxy::platform::sample_gui_pitch(raw, 310);
    selected = kProbe;
    const unsigned saved = raw[5], count = edits;
    check(sdk_sample_edit(raw, 0, 1, 3) == 0 && raw[5] == saved && edits == count);
    for (unsigned value = 0; value <= 2400; value += 37) {
        static const opxy::i32 parameters[8] = {0x40000000}; expected_parameters = parameters;
        raw[5] = opxy::platform::sample_store_settings(value);
        unsigned original[8]; std::memcpy(original, raw, sizeof(raw));
        unsigned guarded[10] = {0x12345678}; guarded[9] = 0x87654321;
        auto* frame = sdk_sample_drum_view(raw, guarded + 1, parameters);
        check(frame && std::strcmp(frame->shifted[0], "first") == 0 && std::strcmp(frame->shifted[1], "second") == 0);
        const auto* copy = reinterpret_cast<const unsigned char*>(guarded + 1);
        check(frame->pitch_cents == 310 + int(value) - 1200);
        check(int(copy[24]) * 100 + int(copy[28]) == 6000 + 310 + int(value) - 1200);
        check((copy[27] & 63) == ((value & 1) ? 3 : 1) && (copy[27] & 192) == 192 &&
              copy[31] == 0);  // the probe inverts the saved direction
        check(guarded[6] == 0 && guarded[0] == 0x12345678 && guarded[9] == 0x87654321);
        check(std::memcmp(raw, original, sizeof(raw)) == 0);
    }
    for (unsigned pass = 0; pass < 2; ++pass) {
        selected = pass ? 3 : kProbe; owned = pass != 0;
        const unsigned before = raw[5], count = edits;
        check(sdk_sample_edit(raw, 0, 1, selected) == 0 && raw[5] == before && edits == count);
    }
    check(!sdk_sample_edit(nullptr, 0, 1, kProbe));
    selected = kProbe; owned = true;
    check(!sdk_sample_edit(raw, 32, 1, kProbe));
}
