#pragma once
#include "types.hpp"

namespace opxy { namespace player {
// A note player transforms the notes a track receives, live or sequenced,
// before they reach its instrument:
//   struct State { ... };
//   bool press(State& state, const Controls& controls, u32 note, u32 velocity, Plan& plan);
//   bool release(State& state, u32 note);
// press either takes the key and describes what sounds (possibly nothing),
// or returns false to let the instrument play it unchanged. Every tone of a
// plan is released together when its key is. release returns true for a key
// press took without sounding (a held control), so nothing else is released.
// Calls are serialized; State starts zeroed and holds no resources.
const u32 max_tones = 8;
const unsigned state_bytes = 2048;
const unsigned state_alignment = 4;

// Delay counts player steps from the key press, 0 sounding on the next step.
struct Tone {
    u8 pitch;
    u8 velocity;
    u16 delay;
};

struct Plan {
    u32 count;
    Tone tones[max_tones];
};

// Optional update(State&, const Controls&, Update&) yields changes to roots
// already sounding after a press or release, and before every player step.
// At most eight updates are drained per event/step, on the sequencer role.
// Replace retriggers the chord; Add preserves existing/queued tones
// and appends pitches not already present, up to max_tones. Updates never start
// a released root. Optional reset(State&) clears held inputs on stock reset or
// all-notes-off; without it the SDK zeroes State.
enum UpdateMode { Replace, Add };
struct Update {
    u32 root;
    UpdateMode mode;
    Plan plan;
};

// The player page's four encoders, Q31 0..1. Declared enums with 2..255 choices
// move one choice per detent. Other mappings retain stock editing: encoder 1
// in 64 steps, encoder 2 in 4, encoder 3 continuously and encoder 4 off/on.
// lowest_key is the note the keyboard's lowest key (F) plays at the track's
// current octave, so note - lowest_key is a key's position: 0..11 the lower
// octave, 12..23 the upper. Sequenced notes keep the pitch they were recorded
// with, so their position follows the octave at playback.
struct Controls {
    i32 value[4];
    u32 lowest_key;
};

// A player with its own page draws it from State on the GUI side, once per
// frame, while the sequencer may be changing State:
//   void display(const State& state, const Controls& controls, Display& display);
// lit shows the artwork's dot k (<name>_dot_<k>, 0..23) when bit k is set;
// text replaces the artwork's <name>_text line; values are the header's four
// encoder values under the definition's labels. Strings are terminated.
const u32 display_dots = 24;
struct Display {
    u32 lit;
    char text[32];
    char values[4][8];
    // Filled by the SDK after display(): emitted notes across all held chords,
    // including strum timing, voicing and spread. Four words cover MIDI 0..127.
    u32 played[4];
    u32 lowest_key;
};
}}
