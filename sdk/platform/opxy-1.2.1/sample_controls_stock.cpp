// Stock OS 1.2.1 sample assignment, drawing controls and drum-page bridges. Each hook replaces a CALL
// to the function it forwards to, so stock calls it with the ordinary C ABI;
// the press and key hooks arrive through drum_page.s and sample_hooks.s.
// Machine policy is in the generated sample callbacks.
#include "sample_machines.hpp"
#include "stock.hpp"
#include "barrier.hpp"

extern "C" {
opxy::u32 sdk_sample_alias(opxy::u32) __asm__("sdk_sample_alias");
opxy::u32 sdk_sample_edit(void*, opxy::u32, opxy::i32, opxy::u32) __asm__("sdk_sample_edit");
opxy::u32 sdk_sample_ui_engine(opxy::u32) __asm__("sdk_sample_ui_engine");
opxy::u32 sdk_sample_parameter_kind(opxy::u32) __asm__("sdk_sample_parameter_kind");
opxy::u32 sdk_sample_controller_engine(opxy::u32) __asm__("sdk_sample_controller_engine");
opxy::u32 sdk_sample_press_engine_owned(opxy::u32, opxy::u32) __asm__("sdk_sample_press_engine_owned");
opxy::u32 sdk_sample_region_control(opxy::u32, opxy::u32, const opxy::u32*, opxy::u32) __asm__("sdk_sample_region_control");
opxy::u32 sdk_sample_selected_engine() __asm__("sdk_sample_selected_engine");
opxy::u32 sdk_sample_live_pitch_kind(opxy::u32) __asm__("sdk_sample_live_pitch_kind");
opxy::u32 sdk_sample_octave_store(opxy::u32, opxy::i32) __asm__("sdk_sample_octave_store");
opxy::u32 sdk_sample_key(opxy::u32, opxy::u32) __asm__("sdk_sample_key");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
}
volatile opxy::i32 sdk_sample_drum_octaves[8];

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::platform;
const u32 kNotOwned = 0xffffffffu;
// The project object stock passes to its parameter editor (8c0b15aa, 8c0a44b0).
const u32 kProject = 0x86bd027c;
// Stock region control: mutation for its own controls, then publication.
u32 region_control(u32 key, u32 control, const u32* event, u32 fine) {
    return stock<u32(u32, u32, const u32*, u32)>(0x8d4dccd8)(key, control, event, fine);
}
// Publish through pitch with a zero delta: keeps stored tuning and machine
// settings. Stock's fade handler would quantize even a zero delta.
u32 publish(u32 key) {
    const u32 zero[3] = {0, 0, 0};
    return region_control(key, 5, zero, 0);
}
void publish_octave(u32 track, i32 octave) {
    if (track >= kTracks) return;
    sdk_sample_drum_octaves[track] = octave;
    barrier();
}
}

// The recorder's target selection, release and save completion ask which
// engine the track uses. Owned sample engines borrow the multisampler's
// branch, or the sampler's with singleSample; the track keeps its real engine.
// A send track's ID is its effect's, which may equal an SDK engine's number:
// leave it to stock.
u32 sdk_sample_ui_engine(u32 track) {
    const u32 id = track_engine(track);
    return track < kTracks ? sdk_sample_alias(id) : id;
}

// 8c0b1316: the instrument controller's encoder turn asks the same question to
// choose between the sample-region controls and the engine parameters. A
// machine drawing its own page keeps its real engine, so its encoders edit the
// parameter vector through sdk_synth_turn, with alternates and fine turns.
// A send track is left to stock, as above.
u32 sdk_sample_controller_engine(u32 track) {
    const u32 id = track_engine(track);
    if (track >= kTracks) return id;
    return own_sample_page(id) ? id : sdk_sample_alias(id);
}

// 8d5acf44: the parameter editor must store drawing-page controls in the
// engine parameter vector, not stock sample-region fields. Other callers of
// the sample predicate retain their assignment/recording behavior.
u32 sdk_sample_parameter_kind(u32 engine) {
    return own_sample_page(engine) ? 0 : stock<u32(u32)>(0x8d5b79c0)(engine);
}

// 8d58f5c2: after stock clamps the octave, mirror its stored value immediately.
// SDK IDs use stock's single-octave keyboard spacing. No engine/project getter:
// this setter also runs while loading a project or preset. A sample lookup may
// be cached, so it cannot be the only place that publishes octave changes.
u32 sdk_sample_octave_store(u32 track, i32 octave) {
    const u32 result = stock<u32(u32, i32)>(0x8d58573c)(track, octave);
    publish_octave(track, octave);
    return result;
}

// Stock's region key lookup (through sample_hooks.s): a single-sample track
// always selects region zero, as stock engine 2 does at 8d589258. A drum page maps
// the note to its canonical pad with the track octave; the GUI-side lookup has
// a valid project context, so it also publishes the octave for audio. Returns
// kNotOwned to let stock continue.
u32 sdk_sample_key(u32 track, u32 key) {
    if (track >= kTracks) return kNotOwned;
    const SampleMachine* machine = sdk_sample_machine(track_engine(track));
    if (!machine) return kNotOwned;
    if (machine->single) return 0;
    if (machine->page != SamplePage::drums) return kNotOwned;
    const i32 octave = stock<i32(u32)>(0x8d58f65c)(track);
    publish_octave(track, octave);
    return stock<u32(u32, i32)>(0x8d5b8688)(key, octave) + 53;
}

// 8d5ad3aa: only the live/step parameter editor's pitch classification.
// Stock checks parameter zero after this call and performs its own cents
// stepping and lock publication. Keep the real engine ID and all other
// parameter paths unchanged; a conflicting/unowned registration stays stock.
u32 sdk_sample_live_pitch_kind(u32 id) {
    if (drum_controls(id)) return 1;
    if (own_sample_page(id)) return 0;  // parameter zero is an ordinary control
    return stock<u32(u32)>(0x8d5b79f0)(id);
}

// 8c0b2730 (through the stub in drum_page.s): an encoder press. button
// is the index minus six (4..7). With drum controls, main first/fourth and
// Shift second/third go to the machine. Presses on a page whose pad records
// hold machine settings never enter stock's mode cycle.
u32 sdk_sample_press_engine_owned(u32 track, u32 button) {
    if (track >= kTracks) return kNoEngine;
    const u32 id = track_engine(track);
    // On an own page the click reaches sdk_synth_press, which selects the
    // position's alternate; stock's sample mode cycle must not run.
    if (own_sample_page(id)) return id;
    if (!pad_settings(id)) return sdk_sample_alias(id);
    const bool shifted = shift_held();
    if (drum_controls(id) && (shifted ? button == 5 || button == 6 : button == 4 || button == 7)) {
        const u32 key = stock<u32(u32)>(0x8d4d6c98)(track);
        if (void* region = pad_region(track, key)) {
            if (sdk_sample_edit(region, button + (shifted ? 8 : 4), 0, id)) publish(key);
        }
    }
    return id;
}

// 8c0b1666: an encoder turn. Stock keeps trim, reverse and volume mutation
// and all publication; the machine receives its own controls. Shift encoders
// 2 and 3 (controls 5 and 6) edit the stock fade word, which holds the pad
// settings of any machine declaring them, so they never reach stock then.
u32 sdk_sample_region_control(u32 key, u32 control, const u32* event, u32 fine) {
    const u32 track = byte(kSelectedTrack);
    const u32 id = track < kTracks ? track_engine(track) : kNoEngine;
    // A machine drawing its own page has no sample-region controls: its
    // encoders (0..3, Shift row 4..7) edit the parameter vector exactly as the
    // instrument controller's own path does, alternates and fine turns included.
    if (own_sample_page(id)) return sdk_synth_turn(kProject, track, control, static_cast<u32>(reinterpret_cast<Address>(event)));
    if ((control == 5 || control == 6) && pad_settings(id) && !drum_controls(id)) return 0;
    if ((control <= 3 || control == 5 || control == 6) && drum_controls(id)) {
        // Middle encoders edit the outer markers through stock's normalizer.
        if (control == 1) return region_control(key, 0, event, fine);
        if (control == 2) return region_control(key, 3, event, fine);
        void* region = pad_region(track, key);
        if (!region || !event[1]) return static_cast<u32>(reinterpret_cast<Address>(region));
        // Keep the engine paired with the active track used for this region.
        // The renderer's cached sample-page track can lag track navigation.
        const u32 index = control | (encoder_held(control & 3) ? 16u : 0u);
        if (sdk_sample_edit(region, index, static_cast<i32>(event[1]), id)) return publish(key);
        return 0;
    }
    return region_control(key, control, event, fine);
}

u32 sdk_sample_selected_engine() {
    const u32 track = selected_track();
    return track < kTracks ? track_engine(track) : kNoEngine;
}
