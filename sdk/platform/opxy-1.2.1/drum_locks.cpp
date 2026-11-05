// Per-hit locks of a padMix kit's Shift fields (drum_locks.hpp), latched at
// note start. GUI role: stock's sequencer copies a track's base words into its
// live words (8d5accf8), writes the step's locks over them (8d5936c0), then
// starts the step's notes (8d595738). For a drum track, note start asks the
// pad payload builder 8d5ad660 (its call at 8d595b50) to fill the eight words
// at 0x86bd025c from the played pad's record and publish them to the audio
// side through 8d5ad194 (its call at 8d5ad776). Both calls are hooked: the
// first notes the note's track, engine and key, and takes the step the
// sequencer's step hook noted for it (drum_lock_store_take); the second folds
// that step's locks for the key (drum_lock_store_note) into the payload on the
// way, after publishing
// the track's live words that changed (8d5acb9c), so a word the sequencer
// restored without publishing, PAN's 38 among them, reaches the audio side
// before the note. Every other caller of the builder (the per-key editor and
// its previews) publishes the key's own values.
//
// With the build option drumlineRow2 the same two hooks serve stock Drumline
// (drumline.hpp): every publication of a Drumline pad carries its row-2
// fields, and a note's is marked for the step locks still to come.
#include "drum_locks.hpp"
#include "drum_lock_store.hpp"
#include "drumline.hpp"
#include "drum_values.hpp"
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_drum_lock_note(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_drum_lock_note");
opxy::u32 sdk_drum_lock_publish(opxy::u32, opxy::u32, opxy::u32, opxy::i32*) __asm__("sdk_drum_lock_publish");
}

namespace {
using opxy::i32;
using opxy::u32;
using opxy::u8;
using namespace opxy::platform;

// The note the builder is filling, while it is.
const u32 kNoTrack = 0xffffffffu;
u32 note_track = kNoTrack, note_key, note_handle, note_line = kNoTrack;
const DrumMachine* note_kit;

i32 stock_gain(i32 db) { return stock<i32(i32)>(0x8d5ad604)(db); }

void fold(const DrumMachine& kit, u32 track, u32 key, i32* payload) {
    DrumHitLocks locks;
    drum_lock_store_note(kit, note_handle, key, locks);
    if (!locks.any()) return;
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, key));
    if (!record) return;
    if (drum_lock_needs_mark(locks) && !drum_marked_q31(payload[0])) {
        u8 model, controls[kDrumControlCount];
        const bool known = drum_region_values(kit, track, key, record, &model, controls) &&
                           drum_lock_mark(payload, record[24], model, controls, drum_codec(kit.model_count, kit.count));
        if (!known)
            for (u32 field = kHitPad1; field < kHitFields; ++field) locks.offset[field] = 0;
    }
    drum_lock_fold(kit, locks, drum_pan(record), drum_level(record), payload, stock_gain);
}
}

// 8d595b50: note start's call of the pad payload builder, for a Drumline
// track (an owned kit's alias): project, track, key, the track's engine and
// three of note start's own pointers, as stock passes them.
u32 sdk_drum_lock_note(u32 project, u32 track, u32 key, u32 engine, u32 a, u32 b, u32 c) {
    const DrumMachine* kit = track < kTracks ? sdk_drum_machine(engine) : nullptr;
    note_kit = kit && kit->pad_mix ? kit : nullptr;
    note_track = note_kit ? track : kNoTrack;
    note_key = key;
    note_handle = note_kit ? drum_lock_store_take(track) : 0;
    note_line = drumline_track(track) ? track : kNoTrack;
    const u32 result = stock<u32(u32, u32, u32, u32, u32, u32, u32)>(0x8d5ad660)(project, track, key, engine, a, b, c);
    note_track = note_line = kNoTrack;
    return result;
}

// 8d5ad776: the builder's one publication of the payload it filled. `kind` is
// the pad's slot, as the region lookup 8d58926c gave it (8d5ad766).
u32 sdk_drum_lock_publish(u32 project, u32 track, u32 kind, i32* payload) {
    if (payload && note_kit && track == note_track) {
        stock<void(u32, u32)>(0x8d5acb9c)(project, track);
        fold(*note_kit, track, note_key, payload);
    }
    if (payload && drumline_track(track)) sdk_drumline->publish(track, kind, payload, track == note_line);
    note_track = note_line = kNoTrack;
    return stock<u32(u32, u32, u32, i32*)>(0x8d5ad194)(project, track, kind, payload);
}

// The step-lock handles' one client check (lock_handles.hpp): an owned padMix kit.
bool opxy::platform::step_locks::handle_client(u32 track) {
    const DrumMachine* kit = track < kTracks ? sdk_drum_machine(track_engine(track)) : nullptr;
    return kit && kit->pad_mix;
}
