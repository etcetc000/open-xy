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

// A player step is stock's player clock (OS 1.2.1): the sequencer calls a
// player once per 8 of its 1920 ticks per quarter note, so there are 240
// steps per quarter note (60 per sixteenth), following the project tempo.
// Read from stock's code (Maestro's step interval and Arpeggio's rate table),
// not yet timed on a device.
const u32 steps_per_quarter = 240;

// Runs a clock of `ticks_per_quarter` ticks per quarter note (e.g. 96) on
// player steps: call it once per step; it returns how many of that clock's
// ticks fall on this step (at most ceil(ticks_per_quarter / 240)). `phase`
// is the clock's own state: zero puts its first tick on the current step.
// A 96-tick clock ticks on steps 0, 2, 5, 7, 10, ...: 2 ticks per 5 steps,
// each within half a step of its exact time and without drift.
inline u32 clock_ticks(u16& phase, u32 ticks_per_quarter) {
    u32 p = phase, n = 0;
    for (; p < ticks_per_quarter; p += steps_per_quarter) ++n;
    phase = static_cast<u16>(p - ticks_per_quarter);
    return n;
}

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
// A player whose press switches the page has four more, value[4..7], which
// edit continuously unless they are enums.
// lowest_key is the note the keyboard's lowest key (F) plays at the track's
// current octave, so note - lowest_key is a key's position: 0..11 the lower
// octave, 12..23 the upper. Sequenced notes keep the pitch they were recorded
// with, so their position follows the octave at playback.
// bpm_x10 is the audio clock's tempo word, in tenths of a BPM (1200 = 120
// BPM), or 0 outside 20..300 BPM; a step then lasts
// 600000000 / (bpm_x10 * steps_per_quarter) microseconds. Not checked under
// external MIDI sync or right after a tempo edit.
//
// SDK 1.16, from the GUI, which publishes them; a callback reads the latest:
// shift is 1 while Shift is held, else 0. It is refreshed once per GUI frame
// (so it lags by up to a frame), on every page, and says nothing about where
// a note came from: a sequenced note pressed while Shift is held sees 1 too.
// shift_value holds the shift row's controls ("shift" in definition.json:
// enums, one to four, or one to eight on a player whose press switches the
// page), each its choice's centre as value[] holds an enum's; 0 beyond the
// row. As on a synth, entry i sits under page i / 4, encoder i % 4: Shift and
// encoder k turn entry row + k, one choice per detent, where row is 0 on the
// first page and 4 on the second. shift_turns[row + k] sums the signed detents
// of every such Shift + turn (wrapping), for relative uses such as a jog or an
// undo; it counts only for a player with a shift row. clicks[k] counts
// (wrapping) clicks of encoder k on the player's page: a press released
// without a turn, without Shift, which the SDK did not take: a paged player's
// press switches its page (while that page is in place), so its clicks stay 0.
// Compare with a copy kept in State to see new ones. Shift values and counters live beside
// State: they start at the row's defaults and zero when the player is built
// (selected, or a project loads) and are not saved with the project.
// The members after lowest_key were appended: value[] and lowest_key keep
// their offsets (0 and 32).
//
// SDK 1.25, appended after clicks: instrument is the kind of instrument the
// player's track holds, as stock classifies it: DrumInstrument for its drum
// sampler, its drum synth or an SDK drum kit; OtherInstrument for anything
// else (a synth, the keyboard sampler, a sample engine, no instrument) and on
// a track outside the eight instrument tracks. Like shift it is published by
// the GUI once per frame and a callback reads the latest, so after the
// track's instrument changes, press, update and display see the new kind
// within a frame. The member exists in a build with a player declaring SDK
// 1.25 or later (the SDK then defines OPXY_PLAYER_INSTRUMENT for its units
// and its players'); a build without one keeps the 112-byte Controls, and
// every older player's code, as they were.
enum Instrument : u32 { OtherInstrument = 0, DrumInstrument = 1 };
struct Controls {
    i32 value[8];
    u32 lowest_key;
    u32 bpm_x10;
    u32 shift;
    i32 shift_value[8];
    i32 shift_turns[8];
    u8 clicks[4];
#if OPXY_PLAYER_INSTRUMENT
    u32 instrument;
#endif
};

// An enum word's choice, 0..choices-1, as the SDK steps it: e.g.
// choice(controls.shift_value[0], 5). 0 for fewer than two choices.
inline u32 choice(i32 value, u32 choices) {
    if (choices < 2) return 0;
    const u32 bucket = ((static_cast<u32>(value < 0 ? 0 : value) >> 16) * choices) >> 15;
    return bucket < choices ? bucket : choices - 1;
}

// A player with its own page draws it from State on the GUI side, once per
// frame, while the sequencer may be changing State:
//   void display(const State& state, const Controls& controls, Display& display);
// lit shows the artwork's dot k (<name>_dot_<k>, 0..23) when bit k is set;
// text replaces the artwork's <name>_text line; values are the encoder
// values under the definition's labels, of which the header shows the current
// page's four. Strings are terminated.
const u32 display_dots = 24;
struct Display {
    u32 lit;
    char text[32];
    char values[8][8];
    // Filled by the SDK after display(): emitted notes across all held chords,
    // including strum timing, voicing and spread. Four words cover MIDI 0..127.
    u32 played[4];
    u32 lowest_key;
};
}}
