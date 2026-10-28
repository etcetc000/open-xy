#pragma once
#include "types.hpp"

namespace opxy {
// A step component takes over one of stock's step components
// (definition.json and machine.json identity "step"): holding shift, a step and
// that component's white key adds it to the step, and the black keys pick one of
// its ten settings. setting is the black key's position, 1..9 then 0 (stock
// shows its labels in that order).
//
// The machine's namespace holds per-track state and the callbacks its
// definition.json "callbacks" lists; stock's own behaviour for the component is
// gone, and an unlisted callback does what an empty stock component does:
//   struct Track { ... };   // one per track, zeroed at every reset
//   void reset(Track& track);                                   // after zeroing
//   bool condition(Track& track);   // whether a "skip component" condition gates it now
//   void enter(Track& track, const opxy::StepContext& at, opxy::u32 setting);
//   opxy::u32 trigs(Track& track, opxy::u32 setting);           // 0: one trig
//   bool mute(Track& track, const opxy::StepContext& at, opxy::u32 setting);
//   void apply(Track& track, const opxy::StepContext& at, opxy::StepNote& note, opxy::u32 setting);
//   bool next(Track& track, const opxy::StepContext& at, opxy::u32 setting, opxy::StepNext& next);
//   bool plays(Track& track, const opxy::StepContext& at, opxy::u32 setting);
//
// When stock calls them, for a step that has the component:
// - reset: at sequencer start for every track, and when a track restarts.
// - enter: each time the playhead enters the step, including repeats and jumps.
// - trigs: at the step's trig, how many evenly spaced trigs to play (2..8 ratchets).
// - mute: at the step's trig; true plays no trig this pass.
// - apply: for every note the step plays, before stock folds pitch into 0..127.
// - next: when the playhead leaves the step. next.step arrives as the natural
//   next step; return true to go to next.step instead (taken modulo the pattern
//   length). Clear next.counts on a repeat so "every Nth" conditions don't count
//   the pass. Components run in stock order (hold, pulse, multiply, velocity,
//   ramps, random, portamento, bend, tonality, jump); the first true wins.
// - condition: before the step's "skip component" condition is checked; false
//   ignores it (stock pulse and hold do so while repeating).
// - plays: only on the three condition keys, which decide whether the step's
//   trig (skip trig), parameter locks (skip param) or other components (skip
//   comp) happen; false skips them. Without plays they always happen. Stock's
//   picker stores black key 2 on these keys as setting 2, like key 3.
// All run in the sequencer's real-time path: keep them short, with no
// allocation, blocking or I/O.
struct StepContext {
    u32 track;           // 0..15
    u32 step;            // 0..length-1
    u32 length;          // the pattern's steps
    u32 ticks_per_step;  // note-length ticks one step spans at the track's speed
};
// pitch is a MIDI note; stock folds it into 0..127 by octaves after every
// component has run, as the SDK does for values below 0. velocity 0..127 is
// clamped, and 0 does not mute (use mute). length is the note's remaining
// length in ticks (ticks_per_step per step), or -1 while a key holds it.
struct StepNote {
    i32 pitch;
    i32 velocity;
    i32 length;
};
struct StepNext {
    u32 step;
    bool counts;
};
const unsigned step_settings = 10;
const unsigned step_tracks = 16;
const unsigned step_track_bytes = 256;  // at most, per track
const unsigned step_max_trigs = 8;
}
