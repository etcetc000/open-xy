// Stock OS 1.2.1 drum-page encoder and key bridge. Each hook replaces a CALL
// to the function it forwards to, so stock calls it with the ordinary C ABI;
// the press and key hooks arrive through drum_page.s and sample_hooks.s.
// Machine policy is in the generated sample callbacks.
#include "sample_machines.hpp"
#include "stock.hpp"
#include "stock_sample.hpp"
#include "sample_preview.hpp"
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
opxy::u32 sdk_sample_slice_start(void*, opxy::u32, opxy::u32, opxy::u32, const void*) __asm__("sdk_sample_slice_start");
opxy::u32 sdk_sample_slice_stop(void*, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_sample_slice_stop");
opxy::u32 sdk_sample_slice_rate(opxy::u32, opxy::u32) __asm__("sdk_sample_slice_rate");
opxy::u32 sdk_sample_slice_note_key(opxy::u32, opxy::u32) __asm__("sdk_sample_slice_note_key");
opxy::u32 sdk_sample_slice_note_route(void*, unsigned char*, unsigned char*, opxy::u32) __asm__("sdk_sample_slice_note_route");
opxy::u32 sdk_sample_slice_send(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_sample_slice_send");
void sdk_sample_slice_receive(void*, const unsigned char*) __asm__("sdk_sample_slice_receive");
void sdk_sample_slice_config(opxy::platform::SamplePreviewRange*, opxy::u32, opxy::u32, const void*) __asm__("sdk_sample_slice_config");
opxy::u32 sdk_sample_slice_clock_rate(opxy::u32, const opxy::platform::SamplePreviewRange*, opxy::u32) __asm__("sdk_sample_slice_clock_rate");
opxy::u32 sdk_sample_create_voice(opxy::u32, void**, opxy::u32, opxy::u32) __asm__("sdk_sample_create_voice");
}
volatile opxy::i32 sdk_sample_drum_octaves[8];

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::platform;
const u32 kNotOwned = 0xffffffffu;
u32 clipboard_engine = kNoEngine;
// These are separate role-local objects. The stock note packet transports the
// source snapshot from GUI to audio; module globals are not a cross-role bus.
struct SliceSource { u32 serial, engine, track, key; SamplePreviewRange range; };
SliceSource slice_source;
// A stopped source may still have a deferred factory when another source
// starts. Keep each played track/key's snapshot until that note is replaced.
volatile SliceSource audio_source[kTracks][128];
// GUI-only context for the synchronous source note-on/off. Musical note
// transforms must not change which sample the slicer auditions. Keeping this
// scoped to the call leaves ordinary keys, sequencing and MIDI routing stock.
struct SliceNote { u32 track, key; };
const SliceNote* slice_note;
// Audio-role context, valid only during the synchronous stock factory call.
const SliceSource* factory_source;
const u32 kSliceMagic = 0x31504c53u; // "SLP1", followed by explicit LE words.
const u32 kSliceWords = 20;
const u32 kSlicePayload = 8 + 4 + 4 * kSliceWords;
template <class Destination, class Source> void copy_range(Destination& to, const Source& from) {
    to.begin = from.begin; to.end = from.end; to.frames = from.frames;
    to.processed = from.processed; to.bpm_x10 = from.bpm_x10;
    to.settings = from.settings; to.level = from.level;
    to.pitch_cents = from.pitch_cents; to.track_transpose = from.track_transpose;
    for (u32 i = 0; i < 8; ++i) to.parameters[i] = from.parameters[i];
}
void put_word(unsigned char* bytes, u32 value) {
    for (u32 i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(value >> (8 * i));
}
u32 get_word(const unsigned char* bytes) {
    u32 value = 0;
    for (u32 i = 0; i < 4; ++i) value |= u32(bytes[i]) << (8 * i);
    return value;
}
void encode_source(unsigned char* bytes, const SliceSource& source) {
    const SamplePreviewRange& r = source.range;
    const u32 words[kSliceWords] = {source.engine, source.track, source.key,
        r.begin, r.end, r.frames, r.processed, r.bpm_x10, r.settings, r.level,
        u32(r.pitch_cents), u32(r.track_transpose),
        u32(r.parameters[0]), u32(r.parameters[1]), u32(r.parameters[2]), u32(r.parameters[3]),
        u32(r.parameters[4]), u32(r.parameters[5]), u32(r.parameters[6]), u32(r.parameters[7])};
    put_word(bytes, kSliceMagic);
    for (u32 i = 0; i < kSliceWords; ++i) put_word(bytes + 4 + 4 * i, words[i]);
}
SliceSource decode_source(const unsigned char* bytes) {
    u32 words[kSliceWords];
    for (u32 i = 0; i < kSliceWords; ++i) words[i] = get_word(bytes + 4 + 4 * i);
    SliceSource source{};
    source.engine = words[0]; source.track = words[1]; source.key = words[2];
    SamplePreviewRange& r = source.range;
    r.begin = words[3]; r.end = words[4]; r.frames = words[5];
    r.processed = words[6]; r.bpm_x10 = words[7]; r.settings = words[8]; r.level = words[9];
    r.pitch_cents = i32(words[10]); r.track_transpose = i32(words[11]);
    for (u32 i = 0; i < 8; ++i) r.parameters[i] = i32(words[12 + i]);
    return source;
}
void publish_audio_source(volatile SliceSource& destination, const SliceSource* source) {
    const u32 serial = (destination.serial + 1u) | 1u;
    destination.serial = serial;
    barrier();
    destination.engine = source ? source->engine : kNoEngine;
    if (source) {
        destination.track = source->track; destination.key = source->key;
        copy_range(destination.range, source->range);
    }
    barrier();
    destination.serial = serial + 1u;
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
// The multi editor enumerates canonical pad indices; the region lookup and
// publisher expect played notes and apply the track octave themselves.
u32 multi_note(u32 track, u32 pad) {
    const i32 octave = stock<i32(u32)>(0x8d58f65c)(track);
    return stock<u32(u32, i32)>(0x8d5b89dc)(pad, octave);
}
}

// 8c0a5e34: the lazy-chop audition. Stock has installed the full source range
// in this pad, but will replace it with cuts while the audition is playing.
u32 sdk_sample_slice_start(void* controller, u32 track, u32 key, u32 velocity, const void* extra) {
    const u32 serial = (slice_source.serial + 1u) | 1u;
    slice_source.serial = serial;
    barrier();
    slice_source.engine = kNoEngine;
    slice_source.range = SamplePreviewRange{};
    const u32 engine = track < kTracks ? track_engine(track) : kNoEngine;
    if (sample_page(engine) == 7 && key < 128) {
        if (const void* raw = pad_region(track, key)) {
            const opxy::SampleRegion region = sample_gui_region(raw);
            slice_source.engine = engine;
            slice_source.track = track;
            slice_source.key = key;
            slice_source.range.begin = region.begin;
            slice_source.range.end = region.end;
            slice_source.range.frames = pad_word(raw, pad_record::kFrames);
            sdk_sample_slice_config(&slice_source.range, engine, track, raw);
        }
    }
    barrier();
    slice_source.serial = serial + 1u;
    const SliceNote note{track, key};
    const SliceNote* previous = slice_note;
    slice_note = slice_source.engine != kNoEngine ? &note : nullptr;
    const u32 result = stock<u32(void*, u32, u32, u32, const void*)>(0x8d594278)(controller, track, key, velocity, extra);
    slice_note = previous;
    return result;
}

// 8c0a5f48: stopping, completing, switching modes or leaving the slicer all
// stop its audition here. Existing preview voices keep their frozen range.
u32 sdk_sample_slice_stop(void* controller, u32 track, u32 key, u32 fourth) {
    const SliceNote note{track, key};
    const bool owned = track < kTracks && key < 128 && sample_page(track_engine(track)) == 7;
    slice_source.serial |= 1u;
    barrier();
    slice_source.engine = kNoEngine;
    barrier();
    ++slice_source.serial;
    const SliceNote* previous = slice_note;
    slice_note = owned ? &note : nullptr;
    const u32 result = stock<u32(void*, u32, u32, u32)>(0x8d5941ac)(controller, track, key, fourth);
    slice_note = previous;
    return result;
}

// 8d595a40 / 8d5955b8: stock transposes non-drum GUI notes before queuing
// note-on/off. Its drum predicate knows only IDs 3/4. Preserve the exact
// source key for an owned slicer audition so audio preview matching and the
// sample lookup use the same pad as the GUI's timestamp conversion.
u32 sdk_sample_slice_note_key(u32 track, u32 key) {
    if (slice_note && slice_note->track == track && slice_note->key == key) return key;
    return stock<u32(u32, u32)>(0x8d58c418)(track, key);
}

// 8d5942cc / 8d5941e2: special keyboard routing can rewrite both argument
// bytes before the transpose step. A source audition belongs to the selected
// sample even in that mode. These callers discard the helper's return value.
u32 sdk_sample_slice_note_route(void* controller, unsigned char* track, unsigned char* key, u32 flag) {
    if (slice_note && track && key && *track == slice_note->track && *key == slice_note->key) return 0;
    return stock<u32(void*, unsigned char*, unsigned char*, u32)>(0x8d5940c6)(controller, track, key, flag);
}

// 8c0a5eb0: both arguments are IEEE float bits (file rate, saved pitch ratio).
// Owned source auditions use their frozen machine clock when provided;
// otherwise they play at file rate. Stock pads retain their multiply.
u32 sdk_sample_slice_rate(u32 rate, u32 pitch) {
    if (slice_source.engine != kNoEngine && !(slice_source.serial & 1u))
        return sdk_sample_slice_clock_rate(slice_source.engine, &slice_source.range, rate);
    return stock<u32(u32, u32)>(0x04021ac0)(rate, pitch);
}

// 8d595aa4: the explicit GUI note path's ordinary eight-byte note message.
// Extend this one packet only while the matching slicer source call is scoped.
// The stock sender copies its payload synchronously, including our snapshot.
u32 sdk_sample_slice_send(u32 track, u32 key, u32 packed, u32 manual, u32 fifth) {
    if (!slice_note || slice_note->track != track || slice_note->key != key || !manual ||
        slice_source.engine == kNoEngine || slice_source.track != track || slice_source.key != key ||
        sample_page(slice_source.engine) != 7)
        return stock<u32(u32, u32, u32, u32, u32)>(0x8d4dc490)(track, key, packed, manual, fifth);
    unsigned char payload[kSlicePayload];
    payload[0] = static_cast<unsigned char>(track);
    payload[1] = static_cast<unsigned char>(key);
    put_word(payload + 2, packed);
    payload[6] = static_cast<unsigned char>(manual);
    payload[7] = static_cast<unsigned char>(fifth);
    encode_source(payload + 8, slice_source);
    return stock<u32(u32, u32, const void*, u32, u32, u32, u32)>(0x11a00ec0)(
        0x841460b4u, 0x98u, payload, kSlicePayload, 0, 255, 255);
}

// Runs at the audio message receiver entry, before stock reads the unchanged
// eight-byte note prefix. A normal note on the same key invalidates stale
// eligibility; a sequencer note cannot select it. Retain the snapshot across
// note-off: stock can still create a pending machine after its note release.
// The next matching ordinary manual note-on clears it before stock runs.
void sdk_sample_slice_receive(void*, const unsigned char* message) {
    if (!message) return;
    const u32 kind = message[2], size = message[3];
    const unsigned char* payload = message + 4;
    if (kind != 0x98 || size < 8) return;
    const u32 track = payload[0], key = payload[1];
    if (track >= kTracks || key >= 128 || !payload[6]) return;
    volatile SliceSource& destination = audio_source[track][key];
    if (destination.engine != kNoEngine) publish_audio_source(destination, nullptr);
    if (size != kSlicePayload || get_word(payload + 8) != kSliceMagic) return;
    const SliceSource source = decode_source(payload + 8);
    const SampleMachine* machine = sdk_sample_machine(source.engine);
    if (source.track != track || source.key != key || source.engine == kNoEngine ||
        !machine || machine->page != 7 || word(0x11b01d48u + track * 4u) != source.engine ||
        source.range.processed > 1 || source.range.begin >= source.range.end ||
        source.range.end > source.range.frames) return;
    if (source.range.processed && (!machine->slice_clock || source.range.bpm_x10 < 200 ||
        source.range.bpm_x10 > 3000)) return;
    publish_audio_source(destination, &source);
}

// 8d51aeb6: output is the parent voice's machine pointer at +0x94. Its +0x56
// byte is the manual-note flag carried by stock's note message, including a
// pending restart. Sequencer voices never enter the source-preview path.
u32 sdk_sample_create_voice(u32 engine, void** output, u32 index, u32 track) {
    const unsigned char* parent = reinterpret_cast<const unsigned char*>(output) - 0x94;
    const volatile SliceSource* received = track < kTracks && parent[0x50] < 128 ?
        &audio_source[track][parent[0x50]] : nullptr;
    SliceSource source{};
    if (received) {
        source.serial = received->serial;
        barrier();
        source.engine = received->engine;
        source.track = received->track;
        source.key = received->key;
        copy_range(source.range, received->range);
        barrier();
    }
    const bool preview = received && !(source.serial & 1u) && source.serial == received->serial &&
        source.engine == engine && engine != kNoEngine && sample_page(engine) == 7 && source.track == track &&
        parent[0x55] == track && parent[0x50] == source.key && parent[0x56] == 1;
    const SliceSource* previous = factory_source;
    factory_source = preview ? &source : nullptr;
    const u32 result = stock<u32(u32, void**, u32, u32)>(0x8d506e92)(engine, output, index, track);
    factory_source = previous;
    return result;
}

const SamplePreviewRange* sdk_sample_slice_preview(u32 engine, u32 track) {
    return factory_source && factory_source->engine == engine && factory_source->track == track ?
        &factory_source->range : nullptr;
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
