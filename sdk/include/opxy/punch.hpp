#pragma once
#include <opxy/types.hpp>

extern "C" void sdk_punch_override(opxy::u32 track, opxy::u32 parameter, opxy::i32 value, void* store)
    __asm__("sdk_punch_override");

// Punch-in effects. An effect takes over one of the twelve punch-in keys. Stock
// still decides which instrument tracks a held key targets (shift + key on one
// track, or the punch-in track's drum and melodic octaves) and records it; the
// effect only decides what happens to those tracks. Every callback is optional
// and runs on the sequencer, never in an audio callback: keep it short, with
// no allocation, blocking or I/O.
namespace opxy { namespace punch {
// How the key reached the track: shift + key on this track, or the punch-in
// track's octave for all drum or all melodic tracks. Stock gives some keys a
// different behaviour per source.
enum class Source : u8 { track, group };

struct Context {
    u32 track;      // instrument track 0..7
    bool drum;      // the track plays a drum engine
    Source source;
    bool first;     // first call of this callback since the key engaged the track
    u32 ticks96;    // sequencer position, 96 ticks per quarter note
    i32 bend;       // stock's punch-in modulation: 2 x the punch-in track's
                    // pitch bend plus 64 x one tilt axis
};

// A note the sequencer is about to play, after step components.
struct Note {
    u8 pitch;       // 0..127; drum tracks select pads by pitch
    u8 velocity;    // 0..127
    bool generated; // a copy made by another punch-in effect
};

// Track parameter overrides. One lasts while it is set on every tick; stock
// releases it on the first tick it is not.
struct Overrides {
    u32 track;
    void* store;
    void set(u32 parameter, i32 value) const { sdk_punch_override(track, parameter, value, store); }
};
const u32 parameter_pan = 38;     // Q31, 0 left .. 1 right
const u32 parameter_level = 39;   // Q31; 0 is silent

struct Effect {
    u8 key;   // 0..11: F, F#, G ... E
    // Each sequencer tick and at each note start, per targeted track.
    void (*tick)(const Context& context, const Overrides& overrides);
    // Per note on a targeted track; edit it in place, or return false to drop it.
    bool (*note)(const Context& context, Note& note);
    // Per track step while the track plays; replace step to play another step
    // of the pattern instead, and return true when it did.
    bool (*step)(const Context& context, i32& step);
};
}}

// The collection's punch-in effects, one per key at most.
extern "C" const opxy::punch::Effect* const sdk_punch_effects[] __asm__("sdk_punch_effects");
extern "C" const opxy::u32 sdk_punch_effect_count __asm__("sdk_punch_effect_count");
