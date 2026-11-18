#pragma once
#include <opxy/types.hpp>

extern "C" void sdk_punch_override(opxy::u32 track, opxy::u32 parameter, opxy::i32 value, void* store)
    __asm__("sdk_punch_override");
extern "C" void sdk_punch_play(opxy::u32 track, opxy::u8 pitch, opxy::u8 velocity, opxy::u32 length,
                               opxy::u32 delay) __asm__("sdk_punch_play");
extern "C" void sdk_punch_master(opxy::u32 parameter, opxy::i32 value) __asm__("sdk_punch_master");
extern "C" opxy::i32 sdk_punch_master_project(opxy::u32 parameter) __asm__("sdk_punch_master_project");
extern "C" void sdk_punch_aux(opxy::u32 parameter, opxy::i32 value) __asm__("sdk_punch_aux");
extern "C" void sdk_punch_track_set(opxy::u32 track, opxy::u32 parameter, opxy::i32 value) __asm__("sdk_punch_track_set");
extern "C" opxy::i32 sdk_punch_track_own(opxy::u32 track, opxy::u32 parameter) __asm__("sdk_punch_track_own");
extern "C" opxy::i32 sdk_punch_live(opxy::u32 track, opxy::u32 parameter) __asm__("sdk_punch_live");
extern "C" void sdk_punch_filter(opxy::u32 track, opxy::u32 type) __asm__("sdk_punch_filter");
extern "C" bool sdk_punch_key_held(opxy::u32 key) __asm__("sdk_punch_key_held");

// Punch-in effects: a second bank beside stock's. On the punch-in track, shift
// + key plays the effect on that key (24 keys: the low octave's twelve from F,
// then the high octave's) on every instrument track; the key alone still plays stock's effect, and shift + key
// on an instrument track still plays stock's effect on that track. Stock never
// sees a second-bank press: it plays no effect for it and records nothing. A
// press holds its effect until that key is released, even if shift is released
// first. Every callback is optional and runs on the sequencer, never in an
// audio callback: keep it short, with no allocation, blocking or I/O.
namespace opxy { namespace punch {
struct Context {
    u32 track;      // instrument track 0..7
    bool drum;      // the track plays a drum engine
    bool first;     // first call of this callback since the key engaged the track
    u32 ticks96;    // sequencer position, 96 ticks per quarter note
    i32 bend;       // stock's punch-in modulation: 2 x the punch-in track's
                    // pitch bend (+-16382) plus 64 x one tilt axis (+-32767).
                    // Stock's effects ignore +-3000 around rest.
    bool lead;      // tick only: the first track this effect runs on this
                    // sequencer tick; set master overrides from it alone
    i32 knob;       // page 2's encoder alone (held and turned), as a bend
                    // size: 0 untouched, intensity_dead_zone + intensity_span
                    // at full. `bend` already includes it; an effect that
                    // must not follow tilt or pitch bend reads this instead.
    i32 turn;       // page 2's encoder signed, per mille: -1000 .. 1000, 0 untouched
                    // (turned below 0 too, unlike knob); for a centred control
};

// Intensity, per mille, from context.bend: 200 (the effect's minimum) at rest
// and within stock's +-3000 dead zone, rising to 1000 as bend or tilt goes
// either way, reached 12000 beyond the dead zone; 600 halfway. Effects take
// their variations from their own keys, never from the bend; the bend sets how
// much. Holding an encoder on page 2 and turning it raises the bend the
// effects see as well (the larger of the two counts).
const i32 intensity_dead_zone = 3000, intensity_span = 12000;
const u32 intensity_low = 200, intensity_half = 600, intensity_high = 1000;
inline u32 intensity(i32 bend) {
    i32 amount = bend < 0 ? -bend : bend;
    amount = amount > intensity_dead_zone ? amount - intensity_dead_zone : 0;
    amount = amount > intensity_span ? intensity_span : amount;
    return intensity_low + (intensity_high - intensity_low) * static_cast<u32>(amount) / intensity_span;
}
// A value that follows the intensity: `low` at 200 (rest), `half` at 600 and
// `high` at 1000, on straight lines between them. Any two of them must differ by no
// more than an i32 holds (Q31 knob values do). 32-bit arithmetic only, so it
// needs no arithmetic runtime.
inline i32 intensity_line(i32 from, i32 to, i32 part, i32 whole) {
    const i32 span = to - from;
    return from + span / whole * part + span % whole * part / whole;
}
inline i32 by_intensity(u32 intensity, i32 low, i32 half, i32 high) {
    if (intensity <= intensity_low) return low;
    if (intensity >= intensity_high) return high;
    if (intensity < intensity_half)
        return intensity_line(low, half, static_cast<i32>(intensity - intensity_low),
                              static_cast<i32>(intensity_half - intensity_low));
    return intensity_line(half, high, static_cast<i32>(intensity - intensity_half),
                          static_cast<i32>(intensity_high - intensity_half));
}

// A note the sequencer is about to play, after step components.
struct Note {
    u8 pitch;       // 0..127; drum tracks select pads by pitch
    u8 velocity;    // 0..127
    bool generated; // played by a punch-in effect, not by the pattern
};

// Track parameter overrides. One lasts while it is set on every tick; stock
// releases it on the first tick it is not.
struct Overrides {
    u32 track;
    void* store;
    void set(u32 parameter, i32 value) const { sdk_punch_override(track, parameter, value, store); }
    // The parameter's live value now: the track's own (its knob) unless an
    // override already holds it, so read it when an effect starts
    // (context.first) and keep it.
    i32 live(u32 parameter) const { return sdk_punch_live(track, parameter); }
    // Switches the track's filter (M3) to `type` and on, for as long as it is
    // set on every tick; then the track's own type and on/off come back.
    void filter(u32 type) const { sdk_punch_filter(track, type); }
};
// Stock's track filter types (M3) and their cutoff and resonance words.
const u32 filter_z_lowpass = 9, filter_z_hipass = 17, filter_dynamic = 18;
// Whether a page-2 key (0..23) is held now, for effects that combine keys.
inline bool key_held(u32 key) { return sdk_punch_key_held(key); }
const u32 parameter_cutoff = 16, parameter_resonance = 17;
const u32 parameter_pan = 38;     // Q31, 0 left .. 1 right
const u32 parameter_level = 39;   // Q31; 0 is silent
// The track's FX I and FX II send knobs, Q31 knob positions (0 .. 0x7fffffff
// for 0 .. 100). Stock's mixer sends L (0.8 k^2 + 0.2 k) into FX I and L k into
// FX II, L being the track's gain, so a full knob sends at the track's level.
const u32 parameter_send_fx1 = 22;
const u32 parameter_send_fx2 = 23;

// The master section's parameters, by MIX page and knob from left to right.
// Page 1 is the stock EQ, which does not run while an SDK master processor is
// selected; eq_extra is the fourth EQ word (MIDI CC 90), which no knob edits.
// Page 3's first two knobs are not yet labelled.
enum class Master : u8 {
    eq_low, eq_mid, eq_high, eq_extra,          // MIX page 1
    dist_gain, dist_clip, dist_tone, dist_mix,  // MIX page 2
    master_a, master_b, compressor, level,      // MIX page 3
};
const u32 master_count = 12;

// Master parameter overrides, global across tracks: set them from `tick` when
// `context.lead` is true. One lasts while it is set on every tick, like
// Overrides; the SDK sends the audio side only values that changed, once per
// sequencer tick, and on the first tick nobody sets it sends the project's own
// value again. The project, its saved state and the knobs are never changed.
// Values are Q31, 0 .. 0x7fffffff as the knob goes from minimum to maximum;
// negative values count as 0. When several effects set one, the last wins.
struct MasterOverrides {
    void set(Master parameter, i32 value) const { sdk_punch_master(static_cast<u32>(parameter), value); }
    // The project's own value (its knob), whatever an override sends.
    i32 project(Master parameter) const { return sdk_punch_master_project(static_cast<u32>(parameter)); }
};
const MasterOverrides master{};

// Aux 5 (track 13, the live input) overrides: its FX I and FX II sends
// (parameter_send_fx1/fx2) and level (parameter_level), so a send effect also
// reaches what the input plays (a riser, say). Global like master: set them
// from `tick` when `context.lead` is true; one lasts while it is set on every
// tick, and on the first tick nobody sets it the value it had before comes
// back. Only the live value changes, never the project or the knob.
struct AuxOverrides {
    void set(u32 parameter, i32 value) const { sdk_punch_aux(parameter, value); }
};
const AuxOverrides aux{};

// The same for any track past the instruments (8..15): the send tracks'
// effect parameters, say (FX I is track 14, FX II track 15; their words 0..7
// the effect's parameters). `own` is the word's value from before the first
// override (or its live value while none holds it), to build on.
const u32 track_fx1 = 14, track_fx2 = 15;
struct TrackOverrides {
    void set(u32 track, u32 parameter, i32 value) const { sdk_punch_track_set(track, parameter, value); }
    i32 own(u32 track, u32 parameter) const { return sdk_punch_track_own(track, parameter); }
};
const TrackOverrides tracks{};

// Plays a note on the context's track `delay` ticks from now (96 per quarter
// note; 0 plays on the next sequencer tick) lasting `length` ticks. It runs
// through step components and the punch-in effects as a generated note. Up to
// 128 notes wait at once across all tracks; more are dropped.
inline void play(const Context& context, u8 pitch, u8 velocity, u32 length, u32 delay = 0) {
    sdk_punch_play(context.track, pitch, velocity, length, delay);
}
const u32 ticks_per_step = 24;    // a sixteenth note

// A flip-dot animation for the punch-in screen. Each frame is 40 x 18 dots,
// row by row from the top left, one bit per dot, least significant bit first
// (90 bytes). The screen advances one frame per refresh and loops. Make one
// from a PNG strip with tooling/punch_animation.py.
const u32 flipdot_columns = 40, flipdot_rows = 18, flipdot_frame_bytes = 90;
struct Animation {
    const u8* const* frames;
    u32 reserved;   // 0
    u32 count;
};
static_assert(sizeof(Animation) == 12, "Stock animation record");

struct Effect {
    // The second-bank key: 0..11 for F, F#, G ... E in the punch-in track's
    // low octave, 12..23 for the same keys in its high octave. At most one
    // effect per key.
    u8 key;
    // Each sequencer tick and at each note start, per instrument track 0..7;
    // check context.drum to act on one kind of track only.
    void (*tick)(const Context& context, const Overrides& overrides);
    // Per note on a targeted track; edit it in place, or return false to drop it.
    bool (*note)(const Context& context, Note& note);
    // Per track step while the track plays; replace step to play another step
    // of the pattern instead, and return true when it did.
    bool (*step)(const Context& context, i32& step);
    // The punch-in screen's animation while the key is held; null keeps stock's.
    const Animation* animation;
    // 0: the effect ends when its key is released. Otherwise it runs on after
    // the release until the sequencer position reaches the next multiple of
    // this many ticks (96 per quarter note; 16 * ticks_per_step is a bar).
    u32 release;
};
const u32 release_bar = 16 * ticks_per_step;
}}

// The collection's punch-in effects, at most one per key.
extern "C" const opxy::punch::Effect* const sdk_punch_effects[] __asm__("sdk_punch_effects");
extern "C" const opxy::u32 sdk_punch_effect_count __asm__("sdk_punch_effect_count");
