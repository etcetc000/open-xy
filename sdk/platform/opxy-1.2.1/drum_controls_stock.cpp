// Stock OS 1.2.1 drum-page encoder and key bridge. Each hook replaces a CALL
// to the function it forwards to, so stock calls it with the ordinary C ABI;
// the press and key hooks arrive through drum_page.s and sample_hooks.s.
// Machine policy is in the generated sample callbacks.
#include "sample_machines.hpp"
#include "stock.hpp"
#include "synth_controls.hpp"
#include "barrier.hpp"

extern "C" {
opxy::u32 sdk_sample_alias(opxy::u32) __asm__("sdk_sample_alias");
opxy::u32 sdk_sample_edit(void*, opxy::u32, opxy::i32, opxy::u32) __asm__("sdk_sample_edit");
opxy::u32 sdk_sample_ui_engine(opxy::u32) __asm__("sdk_sample_ui_engine");
opxy::u32 sdk_sample_press_engine_owned(opxy::u32, opxy::u32) __asm__("sdk_sample_press_engine_owned");
opxy::u32 sdk_sample_region_control(opxy::u32, opxy::u32, const opxy::u32*, opxy::u32) __asm__("sdk_sample_region_control");
opxy::u32 sdk_sample_page_turn(opxy::u32, opxy::u32, const opxy::u32*) __asm__("sdk_sample_page_turn");
opxy::u32 sdk_sample_param_engine(opxy::u32) __asm__("sdk_sample_param_engine");
opxy::u32 sdk_sample_selected_engine() __asm__("sdk_sample_selected_engine");
opxy::u32 sdk_sample_live_pitch_kind(opxy::u32) __asm__("sdk_sample_live_pitch_kind");
opxy::u32 sdk_sample_octave_store(opxy::u32, opxy::i32) __asm__("sdk_sample_octave_store");
opxy::u32 sdk_sample_key(opxy::u32, opxy::u32) __asm__("sdk_sample_key");
}
volatile opxy::i32 sdk_sample_drum_octaves[8];

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::platform;
const u32 kNotOwned = 0xffffffffu;
// The project stock's encoder handlers pass to its parameter editor (8c0b15a2).
const u32 kProject = 0x86bd027c;
// An owned page-2 engine drawing its own screen: its controls are in the synth
// control table (tooling/adapters.py), and its page's encoders edit them.
const SynthControls* own_controls(u32 id) {
    if (sample_page(id) != 2) return nullptr;
    for (u32 i = 0; i < sdk_synth_control_count; ++i)
        if (sdk_synth_controls[i].engine() == id && !sdk_synth_controls[i].effect) return &sdk_synth_controls[i];
    return nullptr;
}
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

// 8c0b1316 and the recorder's target selection, release and save completion
// ask which engine the track uses. Owned sample engines borrow the
// multisampler's branch; the track keeps its real engine. A send track's ID is
// its effect's, which may equal an SDK engine's number: leave it to stock.
u32 sdk_sample_ui_engine(u32 track) {
    const u32 id = track_engine(track);
    return track < kTracks ? sdk_sample_alias(id) : id;
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

// Stock's key-to-region lookup (through sample_hooks.s): stock's sampler (2)
// keeps its one sample in region 0 for every key, the multisampler a region
// per key. An owned page-2 engine plays like the sampler: region 0. An owned
// drum page maps the note to its canonical pad with the track octave; the
// GUI-side lookup has a valid project context, so it also publishes the
// octave for audio. Returns kNotOwned to let stock continue.
u32 sdk_sample_key(u32 track, u32 key) {
    if (track < kTracks && sample_page(track_engine(track)) == 2) return 0;
    if (track >= kTracks || sample_page(track_engine(track)) != 7) return kNotOwned;
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
    return stock<u32(u32)>(0x8d5b79f0)(id);
}

// 8c0b2730 (through the stub in drum_page.s): an encoder press. button
// is the index minus six (4..7). With drum controls, main first/fourth and
// Shift second/third go to the machine. Presses on a page whose pad records
// hold machine settings never enter stock's mode cycle.
u32 sdk_sample_press_engine_owned(u32 track, u32 button) {
    if (track >= kTracks) return kNoEngine;
    const u32 id = track_engine(track);
    // An own screen's press switches its controls' page (synth_controls.cpp), never stock's mode.
    if (own_controls(id)) return id;
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

// 8c0b12f2: the sample page's encoder turn (object, encoder, event) first asks
// 8d5b7c28, a mode flag taking no arguments, and returns at once when it is
// set. On an owned page-2 engine drawing its own screen every turn edits the
// control its position shows (an encoder press switches pages,
// synth_controls.cpp) through stock's parameter editor, which owns stepping,
// locks and publication, and the sample page edits nothing more. Shift turns
// a continuous control in tenths of a step, as on a synth's page. Otherwise
// stock asks its flag as before.
u32 sdk_sample_page_turn(u32 object, u32 encoder, const u32* event) {
    const u32 track = byte(kSelectedTrack);
    const u32 id = track < kTracks ? track_engine(track) : kNoEngine;
    const SynthControls* controls = own_controls(id);
    // The handler reads the encoder as its low two bits; negative means none.
    if (controls && static_cast<i32>(encoder) >= 0 && event) {
        opxy::u8 shown[opxy::page_controls];
        sdk_synth_shown(track, shown);
        const u32 index = shown[encoder & 3];
        const bool continuous = (controls->continuous >> index & 1) != 0;
        const i32* source = reinterpret_cast<const i32*>(event);
        const i32 fine[3] = {source[0] / 10, source[1], source[2]};
        const bool tenths = shift_held() && continuous;
        stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(kProject, track, index,
            static_cast<u32>(reinterpret_cast<Address>(tenths ? fine : source)));
        return 1;
    }
    (void)object;
    return stock<u32()>(0x8d5b7c28)();
}

// 8d5acf3e: stock's parameter writer 8d5acf28 (project, track, index, value)
// asks the track's engine only to classify it. A sample engine's value goes to
// the live table alone, while the editor reads the saved one, so turns never
// add up; any other engine's goes to both. An owned page-2 engine drawing its
// own screen answers 0, which both classifiers reject, so its controls are
// saved and played like a synth's.
u32 sdk_sample_param_engine(u32 track) {
    const u32 id = track_engine(track);
    return own_controls(id) ? 0 : id;
}

u32 sdk_sample_selected_engine() {
    const u32 track = selected_track();
    return track < kTracks ? track_engine(track) : kNoEngine;
}
