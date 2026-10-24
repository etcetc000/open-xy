// Stock OS 1.2.0 drum-page encoder and key bridge. Each hook replaces a CALL
// to the function it forwards to, so stock calls it with the ordinary C ABI;
// the press and key hooks arrive through drum_page.s and sample_hooks.s.
// Machine policy is in the generated sample callbacks.
#include "keyboard_octaves.hpp"
#include "sample_machines.hpp"
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_sample_alias(opxy::u32) __asm__("sdk_sample_alias");
opxy::u32 sdk_sample_edit(void*, opxy::u32, opxy::i32, opxy::u32) __asm__("sdk_sample_edit");
opxy::u32 sdk_sample_ui_engine(opxy::u32) __asm__("sdk_sample_ui_engine");
opxy::u32 sdk_sample_press_engine_owned(opxy::u32, opxy::u32) __asm__("sdk_sample_press_engine_owned");
opxy::u32 sdk_sample_region_control(opxy::u32, opxy::u32, const opxy::u32*, opxy::u32) __asm__("sdk_sample_region_control");
opxy::u32 sdk_sample_selected_engine() __asm__("sdk_sample_selected_engine");
opxy::u32 sdk_sample_live_pitch_kind(opxy::u32) __asm__("sdk_sample_live_pitch_kind");
opxy::u32 sdk_sample_key(opxy::u32, opxy::u32) __asm__("sdk_sample_key");
}

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::platform;
const u32 kNotOwned = 0xffffffffu;
// Stock region control: mutation for its own controls, then publication.
u32 region_control(u32 key, u32 control, const u32* event, u32 fine) {
    return stock<u32(u32, u32, const u32*, u32)>(0x8d4dcab8)(key, control, event, fine);
}
// Publish through pitch with a zero delta: keeps stored tuning and machine
// settings. Stock's fade handler would quantize even a zero delta.
u32 publish(u32 key) {
    const u32 zero[3] = {0, 0, 0};
    return region_control(key, 5, zero, 0);
}
}

// 8c0b1e76 and the recorder's target selection, release and save completion
// ask which engine the track uses. Owned sample engines borrow the
// multisampler's branch; the track keeps its real engine.
u32 sdk_sample_ui_engine(u32 track) { return sdk_sample_alias(track_engine(track)); }

// Stock's drum key lookup (through sample_hooks.s): an owned drum page maps
// the note to its canonical pad with the track octave; the GUI-side lookup has
// a valid project context, so it also publishes the octave for audio. Returns
// kNotOwned to let stock continue.
u32 sdk_sample_key(u32 track, u32 key) {
    if (sample_page(track_engine(track)) != 7) return kNotOwned;
    const i32 octave = stock<i32(u32)>(0x8d58da64)(track);
    sdk_publish_octave(track, octave);
    return stock<u32(u32, i32)>(0x8d5b6800)(key, octave) + 53;
}

// 8d5ab6da: only the live/step parameter editor's pitch classification.
// Stock checks parameter zero after this call and performs its own cents
// stepping and lock publication. Keep the real engine ID and all other
// parameter paths unchanged; a conflicting/unowned registration stays stock.
u32 sdk_sample_live_pitch_kind(u32 id) {
    if (drum_controls(id)) return 1;
    return stock<u32(u32)>(0x8d5b5b68)(id);
}

// 8c0b3290 (through the stub in drum_page.s): an encoder press. button
// is the index minus six (4..7). With drum controls, main first/fourth and
// Shift second/third go to the machine. Presses on a page whose pad records
// hold machine settings never enter stock's mode cycle.
u32 sdk_sample_press_engine_owned(u32 track, u32 button) {
    if (track >= kTracks) return kNoEngine;
    const u32 id = track_engine(track);
    if (!pad_settings(id)) return sdk_sample_alias(id);
    const bool shifted = shift_held();
    if (drum_controls(id) && (shifted ? button == 5 || button == 6 : button == 4 || button == 7)) {
        const u32 key = stock<u32(u32)>(0x8d4d7650)(track);
        if (void* region = pad_region(track, key)) {
            if (sdk_sample_edit(region, button + (shifted ? 8 : 4), 0, id)) publish(key);
        }
    }
    return id;
}

// 8c0b21c6: an encoder turn. Stock keeps trim, reverse and volume mutation
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

u32 sdk_sample_selected_engine() { return track_engine(selected_track()); }
