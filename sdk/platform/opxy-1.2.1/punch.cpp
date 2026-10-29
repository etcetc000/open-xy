// Punch-in effects: routes the keys the collection's effects take over from
// stock's punch-in passes to the effects. Stock still decides which tracks a
// held key targets and records it. GUI/sequencer side; the step hook runs from
// the track player on the same role.
#include <opxy/punch.hpp>
#include "stock.hpp"

namespace {
using opxy::i32;
using opxy::u8;
using opxy::u16;
using opxy::u32;
using namespace opxy::platform;
namespace punch = opxy::punch;

// The sequencer object holds the held-effect mask at +0xc: ten 12-bit words,
// one per instrument track (0-7), then all drum tracks (8) and all melodic
// tracks (9). Bit k is the k-th key from F.
const u32 kBuildMask = 0x8d58c6c0;     // (sequencer)
const u32 kTrackPass = 0x8d58ca10;     // (sequencer, track, bend, overrides)
const u32 kNotePass = 0x8d58cf58;      // (sequencer, track, unused, event, generated)
const u32 kStepRemap = 0x8d58c84c;     // (sequencer, track, step*) -> remapped
const u32 kOverride = 0x8d58c5b8;      // (track, parameter, value, overrides)
const u32 kMelodic = 0x8d587830;       // (track) -> nonzero unless a drum engine
const u32 kBend = 0x8d58c9e4;          // () -> stock's punch-in modulation
// The flip-dot screen picks the latest note held on the punch-in track, maps it
// to one of 24 key positions (the drum octave's twelve, then the melodic
// octave's) and plays that position's animation from this table.
const u32 kAnimation = 0x8d554ae0;     // (node) -> animation record
const u32 kAnimations = 0x8406c4c8;    // 24 animation record pointers
const u32 kMaskOffset = 0xc;
const u32 kGroups = 10;
const u32 kDrumGroup = 8, kMelodicGroup = 9;
const u32 kKeys = 12;
// The note event the sequencer plays: pitch 255 means no note.
const u32 kPitch = 0xd, kVelocity = 0xf;
const u8 kNoNote = 0xff;

u16 owned[kGroups];        // held bits of the keys effects own, taken from stock's mask
u16 engaged[3][kTracks];   // per callback kind: keys that ran on a track last time

const punch::Effect* effect_for(u32 key) {
    for (u32 i = 0; i < sdk_punch_effect_count; ++i)
        if (sdk_punch_effects[i]->key == key) return sdk_punch_effects[i];
    return nullptr;
}

u16 effect_keys() {
    u16 keys = 0;
    for (u32 i = 0; i < sdk_punch_effect_count; ++i)
        if (sdk_punch_effects[i]->key < kKeys) keys |= static_cast<u16>(1u << sdk_punch_effects[i]->key);
    return keys;
}

bool drum(u32 track) { return !stock<u32(u32)>(kMelodic)(track); }

// Runs `call` for every owned key held on `track`, with its context.
template <class Call> bool each(u32 kind, u32 track, i32 bend, Call call) {
    const bool is_drum = drum(track);
    const u16 own = owned[track], group = owned[is_drum ? kDrumGroup : kMelodicGroup];
    const u16 held = own | group, before = engaged[kind][track];
    engaged[kind][track] = held;
    bool any = false;
    for (u32 key = 0; key < kKeys; ++key) {
        const u16 bit = static_cast<u16>(1u << key);
        if (!(held & bit)) continue;
        const punch::Effect* effect = effect_for(key);
        if (!effect) continue;
        const punch::Context context{track, is_drum,
                                     own & bit ? punch::Source::track : punch::Source::group,
                                     !(before & bit), word(kTicks96), bend};
        any |= call(*effect, context);
    }
    return any;
}
}

extern "C" {
void sdk_punch_mask(void* sequencer) __asm__("sdk_punch_mask");
void sdk_punch_track(void* sequencer, u32 track, i32 bend, void* overrides) __asm__("sdk_punch_track");
void sdk_punch_note(void* sequencer, u32 track, u32 unused, u8* event, u32 generated) __asm__("sdk_punch_note");
u32 sdk_punch_step(void* sequencer, u32 track, i32* step) __asm__("sdk_punch_step");
const punch::Animation* sdk_punch_animation(void* node) __asm__("sdk_punch_animation");
}

void sdk_punch_override(u32 track, u32 parameter, i32 value, void* store) {
    if (track < kTracks && parameter < 58) stock<void(u32, u32, i32, void*)>(kOverride)(track, parameter, value, store);
}

// After stock builds the mask, take the effects' keys so stock's own effects
// for them never run. The summary flags stock derives in the same call are not
// adjusted: an effect may not own G, A (repeat flags), C or D (drum fill flag).
void sdk_punch_mask(void* sequencer) {
    stock<void(void*)>(kBuildMask)(sequencer);
    const u16 keys = effect_keys();
    volatile u16* mask = reinterpret_cast<volatile u16*>(static_cast<char*>(sequencer) + kMaskOffset);
    for (u32 group = 0; group < kGroups; ++group) {
        owned[group] = mask[group] & keys;
        mask[group] = mask[group] & static_cast<u16>(~keys);
    }
}

// Stock's per-track parameter pass, then the effects' tick on the same overrides.
void sdk_punch_track(void* sequencer, u32 track, i32 bend, void* overrides) {
    stock<void(void*, u32, i32, void*)>(kTrackPass)(sequencer, track, bend, overrides);
    if (track >= kTracks) return;
    const punch::Overrides out{track, overrides};
    each(0, track, bend, [&](const punch::Effect& effect, const punch::Context& context) {
        if (effect.tick) effect.tick(context, out);
        return false;
    });
}

// Stock's per-note pass over a note about to play, then the effects' note.
void sdk_punch_note(void* sequencer, u32 track, u32 unused, u8* event, u32 generated) {
    stock<void(void*, u32, u32, u8*, u32)>(kNotePass)(sequencer, track, unused, event, generated);
    if (track >= kTracks || event[kPitch] == kNoNote) return;
    const i32 bend = stock<i32()>(kBend)();
    each(1, track, bend, [&](const punch::Effect& effect, const punch::Context& context) {
        if (!effect.note || event[kPitch] == kNoNote) return false;
        punch::Note note{event[kPitch], event[kVelocity], generated != 0};
        if (!effect.note(context, note)) event[kPitch] = kNoNote;
        else event[kPitch] = note.pitch & 0x7f, event[kVelocity] = note.velocity & 0x7f;
        return false;
    });
}

// Stock's step remap for the track player, then the effects' step.
u32 sdk_punch_step(void* sequencer, u32 track, i32* step) {
    u32 remapped = stock<u32(void*, u32, i32*)>(kStepRemap)(sequencer, track, step);
    if (track >= kTracks) return remapped;
    const i32 bend = stock<i32()>(kBend)();
    if (each(2, track, bend, [&](const punch::Effect& effect, const punch::Context& context) {
            return effect.step ? effect.step(context, *step) : false;
        }))
        remapped = 1;
    return remapped;
}

// Stock's animation choice, replaced by an effect's own for the keys it owns.
const punch::Animation* sdk_punch_animation(void* node) {
    const punch::Animation* chosen = stock<const punch::Animation*(void*)>(kAnimation)(node);
    for (u32 position = 0; position < 2 * kKeys; ++position) {
        if (word(kAnimations + 4 * position) != reinterpret_cast<Address>(chosen)) continue;
        const punch::Effect* effect = effect_for(position % kKeys);
        const punch::Animation* own = effect ? effect->animations[position / kKeys] : nullptr;
        return own ? own : chosen;
    }
    return chosen;
}
