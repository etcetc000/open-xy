// Track control (SDK 1.7): a synth that declares resources.trackControl runs
// control() on the GUI side once per stock GUI tick while it is on an
// instrument track, or on an aux track it took over (resources.trackControl.aux).
// It reads every track's live parameters and may overlay other instrument
// tracks' values for that tick, or capture their encoder edits.
// Overlays never reach the saved project: stock keeps its own values, and a
// parameter the machine stops overlaying returns to them.
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
const u32 edit_capacity = 32;
// Words the machine passes to its own display each tick (Overlay::display).
const u32 display_words = 4;
// A machine that declares resources.trackControl.savedBytes (1..saved_capacity)
// keeps data with each project, on the GUI side, through two more functions:
//   u32 control_save(const ControlState&, u8* data, u32 capacity);
// writes at most `capacity` (its savedBytes) bytes when stock saves a project,
// the workspace included, and returns how many; and
//   void control_load(ControlState&, const u8* data, u32 bytes);
// gets them back after stock loads a project saved with them, or `bytes` 0
// after it loads a project without them.
const u32 saved_capacity = 8192;

// One parameter of an instrument track: an overlay entry or a captured edit.
struct Entry { u8 track, index; u16 reserved; i32 value; };

struct Input {
    u32 track;  // the aux track this machine holds (zero-based)
    // Each track's live values as stock sets them (sequenced, locked or
    // edited), without any machine's overlay.
    i32 values[all_tracks][track_parameters];
    // 1 while this copy's capture is in effect: one copy captures at a time.
    u32 capturing;
    // While this copy captures: encoder edits of other instrument tracks'
    // parameters since the last tick, oldest first, with the edited value.
    u32 edit_count;
    Entry edits[edit_capacity];
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
    // its overlays, capture or marks apply then; it still runs, so its page
    // can say so.
    u32 muted;
};

// What the machine returns each tick. An overlay entry for the machine's own
// track, or outside tracks x track_parameters, is ignored; a later entry for
// the same parameter replaces an earlier one, and of two machines overlaying
// one parameter, the one on the later track wins; negative values are treated
// as 0.
struct Overlay {
    u32 count;  // 0 on entry; at most overlay_capacity
    Entry entries[overlay_capacity];
    // 0 on entry. 1 asks to capture encoder edits: they still play and show
    // as usual, and arrive in the next tick's edits. One copy captures at a
    // time: the one already capturing, else the first track asking. When the
    // capture ends, every parameter edited meanwhile returns to its value
    // from before it. While it captures, every page shows the machine's
    // capture marks (a red frame fading inward, unless it supplies its own).
    u32 capture;
    // 0 on entry. 1 marks the pages of the tracks this tick overlays with the
    // machine's line marks (a thin dark red frame, unless it supplies its own).
    u32 mark;
    // 0 on entry: words for the machine's own display, which reads them with
    // from_control. The control and display are separate code, so this is
    // how the display shows what the control worked out.
    i32 display[display_words];
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
}}
