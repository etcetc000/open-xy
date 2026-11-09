// Four of stock's LFOs per instrument track (docs/extensions.md, lfo_copies).
// Stock's own storage (the pattern's LFO type, on flag and track words 24-31)
// holds the LFO its page shows; the other three are the SDK's copies, which
// the audio role runs through stock's own LFO code beside it: every type,
// every destination, every sample.
//
// GUI role: which LFO stock's storage holds, the three copies and which LFO
// each copy slot holds, kept in the project companion (open-xy.lfo-copies) and
// published to the audio role over B3 (state below). Showing another LFO swaps
// it with stock's through stock's own setters, as stock's type list does.
// Audio role: the received copies, their LFO state per stock LFO state, and the
// evaluation around each of stock's LFO calls (multi_lfo_audio.cpp). Every
// change reaches it as a pending change that the render takes at the start of
// the track's next block, so a swap never restarts or doubles an LFO.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace multi_lfo {
const u32 tracks = 8, copies = 3, words = 8, lfos = copies + 1;
// Stock's LFO parameters are track words 24-31 (+0x60 of a track's words).
const u32 first_word = 24;
// Stock's LFO state, per voice and per track.
const u32 state_bytes = 0x7c;

struct Settings {
    u32 on, type;
    i32 words[8];
};
// Stock's LFO types: 0 tremolo, 1 value, 2 random, 3 element, 6 duck.
bool valid_type(u32 type);

// B3 virtual parameters, beside the master processor's 128-137.
// - A copy is a transaction: begin (track | slot << 3 | sequence << 5), flags
//   (on | type << 1), its eight words, then commit (sequence).
// - A swap is one too: begin (track | slot << 3 | sequence << 5); the slot's
//   next settings (the LFO stock's storage held: flags, eight words); stock's
//   next settings (the LFO the slot held: flags, eight words); commit.
// An incomplete one changes nothing.
const u32 message_begin = 160, message_flags = 161, message_word = 162, message_commit = 170;
const u32 swap_begin = 171, swap_slot_flags = 172, swap_slot_word = 173, swap_stock_flags = 181,
          swap_stock_word = 182, swap_commit = 190;

// GUI role.
// Which LFO (0-3) stock's storage holds, and which LFO copy slot `slot` holds.
u32 shown(u32 track);
u32 holder(u32 track, u32 slot);
// A slot's settings, and setting them (false, changing nothing, for
// out-of-range numbers or an unknown type).
const Settings& get(u32 track, u32 slot);
bool set(u32 track, u32 slot, const Settings& settings);
// Show LFO `lfo` (0-3) in stock's storage: stock's settings go to the slot that
// held it, and its settings load through stock's setters. An LFO shown for the
// first time (all zero) starts as a copy of the one it replaces, off. False
// for out-of-range numbers.
bool show(u32 track, u32 lfo);
// Every copy off and cleared, LFO 1 shown, and each copy to be published.
void clear();
// A project load's companion section (open-xy.lfo-copies), byte by byte:
// begin, then each byte, then the end: the section's version when it was read
// whole (0 when not) and whether stock loaded the project. A load that failed
// changes nothing.
void load_begin();
void load_byte(u32 byte);
void load_end(u32 version, bool loaded);
// The GUI tick: after a project load, take its copies (or clear them: one saved
// without them), then publish up to two changed copies to the audio role. Every
// fourth tick in a row that publishes nothing publishes the next slot again
// (all 24 in turn): the audio role can't acknowledge, so a swap or copy it
// never applied heals.
void tick();
// Whether a save has anything to keep, and its bytes (save_bytes of them,
// version save_version): per track, the LFO shown, then per slot the LFO it
// holds, on, type and eight little-endian words.
const u32 save_version = 2;
const u32 track_bytes = 1 + copies * (3 + 4 * words);
const u32 save_bytes = tracks * track_bytes;
// Version 1 (multi-lfo-001): per track and slot on, type and eight words; LFO 1 shown.
const u32 v1_bytes = tracks * copies * (2 + 4 * words);
bool any();
void save(unsigned char* out);
// The stock boundary: one B3 virtual parameter to the audio role, and stock's
// own LFO storage on the track's current pattern: read it, and load it through
// stock's setters (multi_lfo_send.cpp, or a test's doubles).
void send(u32 id, i32 value);
void read_stock(u32 track, Settings& out);
void load_stock(u32 track, const Settings& settings);

// Audio role: a B3 message; true when it was one of ours. A complete
// transaction becomes a pending change of its track.
bool receive(u32 id, i32 value);
struct Change {
    bool swap;
    u32 slot;
    Settings next;   // the slot's next settings
    Settings stock;  // a swap: stock's next settings
};
// The render takes its track's oldest pending change, which sets the slot's
// copy; false when there is none.
bool take(u32 track, Change& out);
// The render, at the start of a track's block: its pending changes, in order.
// A swap exchanges stock's LFO state with the slot's (exchange_states,
// multi_lfo_audio.cpp) and sets stock's audio-side LFO (stock_lfo below).
void apply(u32 track);
void exchange_states(u32 track, u32 slot);
const Settings& audio(u32 track, u32 slot);
}}}
