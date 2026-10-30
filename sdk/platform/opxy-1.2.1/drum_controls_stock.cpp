// Stock OS 1.2.1 drum-page encoder and key bridge. Each hook replaces a CALL
// to the function it forwards to, so stock calls it with the ordinary C ABI;
// the press and key hooks arrive through drum_page.s and sample_hooks.s.
// Machine policy is in the generated sample callbacks.
#include "sample_machines.hpp"
#include "drum_machines.hpp"
#include "stock.hpp"
#include "stock_sample.hpp"
#include "barrier.hpp"

extern "C" {
opxy::u32 sdk_sample_drum_alias(opxy::u32) __asm__("sdk_sample_drum_alias");
opxy::u32 sdk_sample_pad_key_engine(opxy::u32) __asm__("sdk_sample_pad_key_engine");
opxy::u32 sdk_sample_pad_copy(opxy::u32, opxy::u32) __asm__("sdk_sample_pad_copy");
opxy::u32 sdk_sample_pad_paste(opxy::u32, opxy::u32) __asm__("sdk_sample_pad_paste");
opxy::u32 sdk_sample_multi_control(opxy::u32, opxy::u32, const opxy::u32*, opxy::u32) __asm__("sdk_sample_multi_control");
opxy::u32 sdk_sample_multi_press(const opxy::u32*, opxy::u32, opxy::u32) __asm__("sdk_sample_multi_press");
opxy::u32 sdk_sample_alias(opxy::u32) __asm__("sdk_sample_alias");
opxy::u32 sdk_sample_edit(void*, opxy::u32, opxy::i32, opxy::u32) __asm__("sdk_sample_edit");
opxy::u32 sdk_sample_ui_engine(opxy::u32) __asm__("sdk_sample_ui_engine");
opxy::u32 sdk_sample_press_engine_owned(opxy::u32, opxy::u32) __asm__("sdk_sample_press_engine_owned");
opxy::u32 sdk_sample_region_control(opxy::u32, opxy::u32, const opxy::u32*, opxy::u32) __asm__("sdk_sample_region_control");
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
u32 clipboard_engine = kNoEngine;
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
// The multi editor enumerates canonical pad indices; the region lookup and
// publisher expect played notes and apply the track octave themselves.
u32 multi_note(u32 track, u32 pad) {
    const i32 octave = stock<i32(u32)>(0x8d58f65c)(track);
    return stock<u32(u32, i32)>(0x8d5b89dc)(pad, octave);
}
}

// 8c0b1316, the recorder's target selection, release and save completion, and
// the sample page's own engine checks ask which engine the track uses. Owned
// sample engines borrow the branch of the stock engine whose payload they take
// (sample_machines.cpp); the track keeps its real engine. A send track's ID is
// its effect's, which may equal an SDK engine's number: leave it to stock.
u32 sdk_sample_ui_engine(u32 track) {
    const u32 id = track_engine(track);
    return track < kTracks ? sdk_sample_alias(id) : id;
}

// 8c0b2e68 / 8c0b214e: paired key-down/key-up handlers. The former enables
// stock's held-pad actions; the latter must also recognize the engine so
// releasing the pad clears the overlay. Stock copies the complete pad record
// for both clipboard and slices, retaining the tagged settings word.
u32 sdk_sample_pad_key_engine(u32 track) {
    const u32 id = track_engine(track);
    return track < kTracks ? sdk_sample_drum_alias(id) : id;
}

// Stock owns the clipboard's complete 128-byte region (including its path),
// canonical destination key, cache refresh and publication. Its fade word is
// machine-specific: never give a stock sampler the SDK's tagged payload, or
// interpret one SDK machine's settings as another's. Both calls are GUI-only.
u32 sdk_sample_pad_copy(u32 track, u32 key) {
    const u32 result = stock<u32(u32, u32)>(0x8d581f34)(track, key);
    clipboard_engine = result ? track_engine(track) : kNoEngine;
    return result;
}
u32 sdk_sample_pad_paste(u32 track, u32 key) {
    u32 (*const paste)(u32, u32) = stock<u32(u32, u32)>(0x8d581f90);
    if (track >= kTracks || word(0x8409fa2c) != 4) return paste(track, key);
    u32* const clipboard = at<u32>(0x8409fb1c);
    const u32 original = clipboard[pad_record::kFade];
    const u32 engine = track_engine(track);
    const SampleMachine* m = sdk_sample_machine(engine);
    const bool tagged = sample_settings(original).saved;
    if (m && m->page == 7 && m->defaults) {
        if (!tagged || engine != clipboard_engine)
            clipboard[pad_record::kFade] = sample_store_settings(m->defaults());
    } else if (tagged) {
        clipboard[pad_record::kFade] = 0;
    }
    const u32 result = paste(track, key);
    clipboard[pad_record::kFade] = original;
    return result;
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
    if (track >= kTracks) return kNotOwned;
    const u32 engine = track_engine(track);
    if (sample_page(engine) == 2) return 0;
    if (sample_page(engine) != 7 && !sdk_drum_machine(engine)) return kNotOwned;
    const i32 octave = stock<i32(u32)>(0x8d58f65c)(track);
    publish_octave(track, octave);
    return stock<u32(u32, i32)>(0x8d5b8688)(key, octave) + 53;
}

// 8d5ad3aa: only the live/step parameter editor's pitch classification.
// Stock checks parameter zero after this call and performs its own cents
// stepping and lock publication. Keep the real engine ID and all other
// parameter paths unchanged; a conflicting/unowned registration stays stock.
u32 sdk_sample_live_pitch_kind(u32 id) {
    if (sdk_drum_machine(id)) return 0;
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

// 8c094710: stock's edit-multiple controller already iterates its selection
// mask. Its separate drum mutator otherwise edits mode/pan/fade, bypassing
// the sample-machine callback and corrupting the settings in the fade word.
u32 sdk_sample_multi_control(u32 key, u32 control, const u32* event, u32 fine) {
    const u32 track = byte(kSelectedTrack);
    const u32 id = track < kTracks ? track_engine(track) : kNoEngine;
    if (drum_controls(id)) {
        if (key < 53 || key >= 77 || !event) return 0;
        return sdk_sample_region_control(multi_note(track, key - 53), control, event, fine);
    }
    if ((control == 5 || control == 6) && pad_settings(id)) return 0;
    return stock<u32(u32, u32, const u32*, u32)>(0x8d4d63b0)(key, control, event, fine);
}

// 8c0949e0 (through drum_page.s): encoder click in the same controller.
// Word two is its 24-pad
// selection mask. Apply the same per-pad click as the ordinary sample page;
// no stock mode-bit cycle is allowed on a machine that owns pad settings.
u32 sdk_sample_multi_press(const u32* controller, u32 track, u32 button) {
    const u32 id = track < kTracks ? track_engine(track) : kNoEngine;
    if (!controller || !drum_controls(id) || button < 10 || button > 13) return id;
    const u32 knob = button - 10;
    const bool shifted = shift_held();
    if (shifted ? knob != 1 && knob != 2 : knob != 0 && knob != 3) return id;
    const u32 mask = controller[2] & 0x00ffffffu;
    for (u32 pad = 0; pad < 24; ++pad) {
        if (!(mask & (1u << pad))) continue;
        const u32 note = multi_note(track, pad);
        if (void* region = pad_region(track, note)) {
            if (sdk_sample_edit(region, knob | 8u | (shifted ? 4u : 0u), 0, id)) publish(note);
        }
    }
    // The original engine query must still deliver the real ID: stock ID 3
    // continues its mode-bit cycle; SDK IDs finish without that mutation.
    return id;
}

u32 sdk_sample_selected_engine() {
    const u32 track = selected_track();
    return track < kTracks ? track_engine(track) : kNoEngine;
}
