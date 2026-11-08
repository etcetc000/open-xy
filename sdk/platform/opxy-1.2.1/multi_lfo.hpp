// Copies of stock's LFO (docs/extensions.md, lfo_copies). Each instrument track
// has stock's own LFO and up to three copies, which the audio role runs through
// stock's own LFO code beside it: every type, every destination, every sample.
//
// GUI role: the copies as an extension set them, kept in the project companion
// (open-xy.lfo-copies) and published to the audio role over B3 (state below).
// Audio role: the received copies, their LFO state per stock LFO state, and the
// evaluation around each of stock's LFO calls (multi_lfo_audio.cpp).
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace multi_lfo {
const u32 tracks = 8, copies = 3, words = 8;
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

// B3 virtual parameters, beside the master processor's 128-137. One copy is a
// transaction: begin (track | copy << 3 | sequence << 5), flags (on | type << 1),
// its eight words, then commit (sequence). An incomplete one changes nothing.
const u32 message_begin = 160, message_flags = 161, message_word = 162, message_commit = 170;

// GUI role.
const Settings& get(u32 track, u32 copy);
// False, changing nothing, for out-of-range numbers or an unknown type.
bool set(u32 track, u32 copy, const Settings& settings);
// Every copy off and cleared, and each to be published.
void clear();
// A project load's companion section (open-xy.lfo-copies), byte by byte:
// begin, then each byte, then the end: whether the section was read whole and
// whether stock loaded the project. A load that failed changes nothing.
void load_begin();
void load_byte(u32 byte);
void load_end(bool whole, bool loaded);
// The GUI tick: after a project load, take its copies (or clear them: one saved
// without them), then publish up to two changed copies to the audio role.
void tick();
// Whether a save has copies to keep, and their bytes (save_bytes of them).
const u32 save_bytes = tracks * copies * (2 + 4 * words);
bool any();
void save(unsigned char* out);
// The stock boundary: one B3 virtual parameter to the audio role.
void send(u32 id, i32 value);

// Audio role: a B3 message; true when it was one of ours.
bool receive(u32 id, i32 value);
const Settings& audio(u32 track, u32 copy);
}}}
