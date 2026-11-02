// Punch-in effects: routes the keys the collection's effects take over from
// stock's punch-in passes to the effects. Stock still decides which tracks a
// held key targets and records it. GUI/sequencer side; the step hook runs from
// the track player on the same role.
#include <opxy/punch.hpp>
#include "stock.hpp"

namespace {
using opxy::i32;
using opxy::i64;
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
// Stock's note player, as its own repeat and fill keys call it: (sequencer,
// track, pitch, length (2 words), velocity, 0, 0, time (2 words), flags, 0).
// Length counts 480 per step; flag 4 marks a note a punch-in effect generated.
const u32 kPlay = 0x8d595738;
const u32 kNow = 0x8d59357c;           // () -> 64-bit event time
const u32 kSequencer = 0x860c6adc;
const u32 kPlayGenerated = 4;
const u32 kPlayTicksPer96 = 20;        // 480 per step, 24 ticks96 per step
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
// Summary flags stock's mask builder derives from the mask: a repeat key (G or
// A) held on any track's own word, and a fill key (C or D) held for a drum track.
const u32 kRepeatFlag = 0x20, kFillFlag = 0x21;
const u16 kRepeatKeys = 1u << 2 | 1u << 4, kFillKeys = 1u << 7 | 1u << 9;

u16 owned[kGroups];        // held bits of the keys effects own, taken from stock's mask
u16 engaged[3][kTracks];   // per callback kind: keys that ran on a track last time

// Notes effects asked to play, played from the start of a sequencer tick.
struct Pending { u32 due, length; u8 track, pitch, velocity, waiting; };
const u32 kPending = 64;
Pending pending[kPending];
u32 last_ticks;
bool playing_generated;    // inside stock's note player for one of our notes

void play_due() {
    const u32 now = word(kTicks96);
    if (now < last_ticks)  // the position went back: drop what was waiting
        for (u32 i = 0; i < kPending; ++i) pending[i].waiting = 0;
    last_ticks = now;
    for (u32 i = 0; i < kPending; ++i) {
        Pending& p = pending[i];
        if (!p.waiting || static_cast<i32>(now - p.due) < 0) continue;
        p.waiting = 0;
        const i64 time = stock<i64()>(kNow)();
        playing_generated = true;
        stock<void(void*, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32)>(kPlay)(
            reinterpret_cast<void*>(kSequencer), p.track, p.pitch, p.length * kPlayTicksPer96, 0, p.velocity, 0, 0,
            static_cast<u32>(time), static_cast<u32>(static_cast<unsigned long long>(time) >> 32), kPlayGenerated, 0);
        playing_generated = false;
    }
}

// kind 0: drum tracks, 1: melodic tracks.
const punch::Effect* effect_for(u32 key, u32 kind) {
    for (u32 i = 0; i < sdk_punch_effect_count; ++i)
        if (sdk_punch_effects[i]->keys[kind] == key) return sdk_punch_effects[i];
    return nullptr;
}

u16 effect_keys(u32 kind) {
    u16 keys = 0;
    for (u32 i = 0; i < sdk_punch_effect_count; ++i)
        if (sdk_punch_effects[i]->keys[kind] < kKeys) keys |= static_cast<u16>(1u << sdk_punch_effects[i]->keys[kind]);
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
        const punch::Effect* effect = effect_for(key, is_drum ? 0 : 1);
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
void sdk_punch_play(u32 track, u8 pitch, u8 velocity, u32 length, u32 delay) __asm__("sdk_punch_play");
const punch::Animation* sdk_punch_animation(void* node) __asm__("sdk_punch_animation");
}

void sdk_punch_override(u32 track, u32 parameter, i32 value, void* store) {
    if (track < kTracks && parameter < 58) stock<void(u32, u32, i32, void*)>(kOverride)(track, parameter, value, store);
}

void sdk_punch_play(u32 track, u8 pitch, u8 velocity, u32 length, u32 delay) {
    if (track >= kTracks || pitch > 127) return;
    for (u32 i = 0; i < kPending; ++i) {
        if (pending[i].waiting) continue;
        pending[i] = Pending{word(kTicks96) + delay, length ? length : 1, static_cast<u8>(track), pitch,
                             static_cast<u8>(velocity & 0x7f), 1};
        return;
    }
}

// After stock builds the mask, take the effects' keys so stock's own effects
// for them never run: drum keys from drum tracks' words and the drum group,
// melodic keys from the others. Then derive stock's summary flags again, as
// its builder does, from the keys left to stock.
void sdk_punch_mask(void* sequencer) {
    stock<void(void*)>(kBuildMask)(sequencer);
    char* base = static_cast<char*>(sequencer);
    volatile u16* mask = reinterpret_cast<volatile u16*>(base + kMaskOffset);
    const u16 keys[2] = {effect_keys(0), effect_keys(1)};
    bool track_drum[kTracks];
    for (u32 group = 0; group < kGroups; ++group) {
        const bool is_drum = group < kTracks ? (track_drum[group] = drum(group)) : group == kDrumGroup;
        const u16 take = keys[is_drum ? 0 : 1];
        owned[group] = mask[group] & take;
        mask[group] = mask[group] & static_cast<u16>(~take);
    }
    bool repeat = false, fill = false;
    for (u32 track = 0; track < kTracks; ++track) {
        repeat |= (mask[track] & kRepeatKeys) != 0;
        if (track_drum[track]) fill |= ((mask[track] | mask[kDrumGroup]) & kFillKeys) != 0;
    }
    *reinterpret_cast<volatile u8*>(base + kRepeatFlag) = repeat;
    *reinterpret_cast<volatile u8*>(base + kFillFlag) = fill;
    play_due();
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
        punch::Note note{event[kPitch], event[kVelocity], generated != 0 || playing_generated};
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
        const punch::Effect* effect = effect_for(position % kKeys, position / kKeys);
        const punch::Animation* own = effect ? effect->animations[position / kKeys] : nullptr;
        return own ? own : chosen;
    }
    return chosen;
}
