// The audio role's evaluation of the copies of stock's LFO (multi_lfo.hpp).
//
// Stock evaluates its LFO with 11a0446e(state, track, words, pass) at seven
// sites: per voice in both voice renderers (pass 0 before the voice's
// envelope reads its words, pass 1 after its per-sample buffers fill), and per
// track in the audio loop (pass 2 before the voices, pass 4 after them, and
// pass 3, which matches any pass, for the selected track when nothing else
// ran). The evaluator returns at once unless the pass is the LFO's own.
//
// Around each of those calls the SDK runs every copy that is on first, with
// its own state and its own settings swapped into stock's (the type table and
// the track's LFO words 24-31), then stock's LFO when it is on. Destinations:
// - Words (engine, envelope, filter): stock's 8d518ec4 adds into the track's
//   audio words and keeps the original in ONE slot per track, which its
//   restore puts back after the voice (or the track). A copy's slot is taken
//   out before stock's LFO runs and kept on the SDK's stack, which restores
//   it after stock's own restore: last in, first out.
// - Per-sample buffers (pitch, amplitude, pan): stock scales them in place,
//   buf *= 1 + lfo, so copies and stock's LFO compose.
// Stock's selected-track telemetry (the knob animation) stays stock's LFO's.
#pragma once
#include "multi_lfo.hpp"
#include "stock.hpp"

namespace opxy { namespace platform { namespace multi_lfo {
// Scopes of the save stack: a voice, a track before its voices (passes 2 and
// 3) and a track after them (pass 4).
enum Scope : u32 { voice_scope = 0, track_before = 1, track_after = 2 };

// The renderers' per-voice passes. Pass 0 returns nothing: both passes always
// run, since copies may use either. Pass 1 returns whether the voice needs its
// restore (stock's LFO applied, or a copy's words are to be put back).
void voice_pass0(Address state, u32 track, Address words);
u32 voice_pass1(Address state, u32 track, Address words);
// After the voice: stock's restore when its LFO applied, then the copies'.
void voice_restore(u32 track);
// A track's pass (2, 3 or 4): whether it needs its restore.
u32 track_pass(Address state, u32 track, Address words, u32 pass);
// After the track's voices (after), or at its end (before).
void track_restore(u32 track, Scope scope);
// A note start: stock's reset of its LFO state, then each copy's.
void reset(Address state, u32 track, Address words);
// Stock's selected-track telemetry, only for stock's own LFO.
void telemetry(u32 track, u32 index, i32 wave, i32 amount);

// The stock boundary (multi_lfo_audio_stock.cpp, or a test's doubles).
namespace stock_lfo {
u32 evaluate(Address state, u32 track, Address words, u32 pass);
void reset(Address state, u32 track, Address words);
void restore(u32 track);
void telemetry(u32 track, u32 index, i32 wave, i32 amount);
bool on(u32 track);
// Stock's audio-side LFO type table, the track's words and its save slot.
u32& type(u32 track);
i32* words(u32 track);
i32& slot_index(u32 track);
i32& slot_original(u32 track);
}
}}}
