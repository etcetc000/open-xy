// Track control (SDK 1.9): a synth that declares resources.trackControl runs
// control() on the GUI side once per stock GUI tick while it is on an
// instrument track, or on an aux track it took over (resources.trackControl.aux).
// It reads every track's live parameters and may overlay other instrument
// tracks' values for that tick. A step's parameter lock always plays as written:
// no overlay is written over a parameter a lock holds.
// Overlays never reach the saved project: stock keeps its own values, and a
// parameter the machine stops overlaying returns to them. A stored write
// (store) changes stock's own value instead, as an encoder edit does.
//
// Every track control machine on tracks 0..7, then on the aux tracks, runs in
// track order, each copy with its own state. A machine that declares
// "unique": true runs only while one track holds it; one that keeps data with
// projects, or takes over an aux track, must be unique.
#pragma once
#include <opxy/transport.hpp>
#include <opxy/types.hpp>

extern "C" opxy::u32 sdk_control_copies(opxy::u32 track);
extern "C" opxy::i32 sdk_control_display(opxy::u32 index);

namespace opxy { namespace control {
// Instrument tracks 0..7, which machines steer; all 16 tracks, aux tracks
// 8..15 included, which they read; and stock's live parameter words for each:
// the engine's eight controls (0..7), then stock's own track parameters in
// stock's units. Every live value is non-negative.
const u32 tracks = 8;
const u32 all_tracks = 16;
const u32 track_parameters = 58;
const u32 overlay_capacity = 64;
// Stored writes a machine may make in one tick (store).
const u32 store_capacity = 16;
// Words the machine passes to its own display each tick (Overlay::display).
const u32 display_words = 16;
// Notes a machine may start in one tick, and notes every machine together
// may hold sounding; the longest a note sounds.
const u32 note_capacity = 32;
const u32 held_capacity = 48;
const u32 longest_note_us = 8000000;
// Turns of the machine's own encoders one tick can deliver.
const u32 turn_capacity = 16;
// A machine that declares resources.trackControl.savedBytes (1..saved_capacity)
// keeps data with each project, on the GUI side, through two more functions:
//   u32 control_save(const ControlState&, u8* data, u32 capacity);
// writes at most `capacity` (its savedBytes) bytes when stock saves a project,
// the workspace included, and returns how many; and
//   void control_load(ControlState&, const u8* data, u32 bytes);
// gets them back after stock loads a project saved with them, or `bytes` 0
// after it loads a project without them.
const u32 saved_capacity = 8192;

// One parameter of an instrument track: an overlay entry or a stored write.
struct Entry { u8 track, index; u16 reserved; i32 value; };
// A note to play on instrument track `track` (0..7): MIDI pitch 0..127,
// velocity 1..127, sounding for length_us.
struct Note { u8 track, pitch, velocity, reserved; u32 length_us; };
// A detent of one of the machine's own encoders: the parameter slot its page
// position shows (0..7) and the direction, +1 or -1.
struct Turn { u8 slot, reserved; i16 direction; };

struct Input {
    u32 track;  // the aux track this machine holds (zero-based)
    // Each track's live values as stock sets them (sequenced, locked or
    // edited), without any machine's overlay.
    i32 values[all_tracks][track_parameters];
    // Microseconds since the previous tick (0 on the first after a reset, at
    // most one second), for anything that runs in real time.
    u32 elapsed_us;
    // The project's tempo and the song position (96 ticks per quarter note,
    // advancing only while playing), for anything that follows the music.
    Transport transport;
    // Last directly handled page key since the last tick: 1..4 for M1..M4,
    // or 0. M1 selects the machine page; M2/M3 leave the current page alone.
    // With sharedLfo enabled, M4 belongs to the shared LFO and is not delivered.
    u32 page_key;
    // 1 while the machine's track is muted in the current scene (aux + track
    // key, or the mixer). As stock's brain does nothing while muted, none of
    // its overlays apply then; it still runs, so its page
    // can say so.
    u32 muted;
    // 1 while Shift is held.
    u32 shift;
    // While this copy takes its own turns (Overlay::own_turns): detents of
    // its track's encoders since the last tick, oldest first.
    u32 turn_count;
    Turn turns[turn_capacity];
};

// What the machine returns each tick. An overlay entry for the machine's own
// track, or outside tracks x track_parameters, is ignored; a later entry for
// the same parameter replaces an earlier one, and of two machines overlaying
// one parameter, the one on the later track wins; negative values are treated
// as 0.
struct Overlay {
    u32 count;  // 0 on entry; at most overlay_capacity
    Entry entries[overlay_capacity];
    // 0 on entry: words for the machine's own display, which reads them with
    // from_control. The control and display are separate code, so this is
    // how the display shows what the control worked out.
    i32 display[display_words];
    // 0 on entry; at most store_capacity. Stored writes (store), applied in
    // order after control() returns.
    u32 stored_count;
    Entry stored[store_capacity];
    // 0 on entry: notes to start now, through stock's live note-on, as a
    // track's keyboard would play them but never recorded. Each ends after
    // its length (at most longest_note_us), when the sequencer stops, or when
    // the copy leaves its track; one on a track and pitch this copy already
    // holds ends that one first. A note outside the ranges, or beyond
    // held_capacity sounding notes from every machine, is dropped.
    u32 note_count;
    Note notes[note_capacity];
    // 0 on entry. 1 asks for the next tick's turns of the machine's own
    // encoders: while it is in effect they edit nothing and arrive in
    // Input::turns instead.
    u32 own_turns;
};

// GUI side, for a display: how many tracks hold the same machine as `track`,
// `track` included. A unique machine runs only while exactly one does.
// Browser previews answer 1.
inline u32 copies(u32 track) { return sdk_control_copies(track); }
// GUI side, for a display: word `index` of what the control on the selected
// track last returned in Overlay::display, or 0 when it did not run then.
// Browser previews answer 0.
inline i32 from_control(u32 index) { return sdk_control_display(index); }

inline void add(Overlay& overlay, u32 track, u32 index, i32 value) {
    if (overlay.count >= overlay_capacity || track >= tracks || index >= track_parameters) return;
    Entry& entry = overlay.entries[overlay.count++];
    entry.track = static_cast<u8>(track);
    entry.index = static_cast<u8>(index);
    entry.reserved = 0;
    entry.value = value;
}

// Sets parameter `index` of instrument track `track` (0..7) to `value` as stock
// keeps it: the stored value of the track's current pattern, which the project
// saves and the page shows, as an encoder edit sets it. Applied after control()
// returns, in order; `values` holds it from the next tick. False, and nothing
// is written, when this tick already holds store_capacity stored writes or the
// parameter is outside tracks x track_parameters. The SDK ignores a write to
// the machine's own track, and every write while the machine's track is muted;
// a negative value counts as 0. Under an overlay, the write is what the overlay's
// release restores. A step's lock on the parameter plays on; stock's next step
// plays the stored value.
inline bool store(Overlay& overlay, u32 track, u32 index, i32 value) {
    if (overlay.stored_count >= store_capacity || track >= tracks || index >= track_parameters) return false;
    Entry& entry = overlay.stored[overlay.stored_count++];
    entry.track = static_cast<u8>(track);
    entry.index = static_cast<u8>(index);
    entry.reserved = 0;
    entry.value = value;
    return true;
}

inline void play(Overlay& overlay, u32 track, u32 pitch, u32 velocity, u32 length_us) {
    if (overlay.note_count >= note_capacity) return;
    Note& note = overlay.notes[overlay.note_count++];
    note.track = static_cast<u8>(track < 256 ? track : 255);
    note.pitch = static_cast<u8>(pitch < 256 ? pitch : 255);
    note.velocity = static_cast<u8>(velocity < 256 ? velocity : 255);
    note.reserved = 0;
    note.length_us = length_us;
}
}}
