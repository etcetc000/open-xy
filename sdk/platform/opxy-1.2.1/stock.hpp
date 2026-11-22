// Stock OS 1.2.1 functions and globals that more than one platform unit uses.
// Addresses used by a single unit stay beside that unit's code.
#pragma once
#include <opxy/transport.hpp>
#include <opxy/types.hpp>

namespace opxy { namespace platform {
typedef __UINTPTR_TYPE__ Address;

// A stock object or function at an absolute address; functions use the C ABI.
template <class T> T* at(Address address) { return reinterpret_cast<T*>(address); }
template <class Function> Function* stock(u32 address) { return at<Function>(address); }
inline u32 word(u32 address) { return *reinterpret_cast<const volatile u32*>(address); }
inline u32 byte(u32 address) { return *reinterpret_cast<const volatile unsigned char*>(address); }

// Globals.
const u32 kSelectedTrack = 0x840a0534;     // byte: the GUI's selected track
const u32 kScreen = 0x8400b9d0;            // current GUI screen
const u32 kSampleBrowserScreen = 4;        // kScreen while the sample browser is open
const u32 kAudioBpm = 0x84c69824;          // audio clock tempo, IEEE-754 binary32 bits
const u32 kPitchIncrements = 0x11902c00;   // 64 per-sample phase increments of the voice being rendered
const u32 kCurrentSample = 0x84052fc0;     // sample object the allocator publishes before a factory call
const u32 kVoiceDefaultMethod = 0x8d5051c4;  // stock voice method-table entry +40
const u32 kTracks = 8;
const u32 kNoEngine = 33;                  // stock "none" engine ID

// The audio clock's tempo as Transport::bpm_x10, decoding its positive binary32
// BPM without floating-point helpers; 0 outside 20..300 BPM. The bounded
// exponent gives shifts 15..19; mantissa*10 and rounding fit u32.
inline u32 audio_bpm_x10() {
    const u32 bits = word(kAudioBpm);
    if (bits < 0x41a00000u || bits > 0x43960000u) return 0;
    const u32 shift = 150u - (bits >> 23);
    return (((bits & 0x007fffffu) | 0x00800000u) * 10u + (1u << (shift - 1u))) >> shift;
}

// Audio-role transport, read once per block without floating-point work.
const u32 kPlaying = 0x860c9b39;  // two bytes; either set while playing
const u32 kTicks96 = 0x86bd0080;
inline Transport audio_transport() {
    return Transport{audio_bpm_x10(), word(kTicks96), byte(kPlaying) || byte(kPlaying + 1)};
}

// Assignment runs on the GUI side. Read the project's stored tenths of BPM,
// as the internal-clock branch of stock's tempo display does (8d57ff6c).
// The audio clock is a separate cached value and may not reflect GUI edits.
inline u32 project_bpm_x10() {
    const volatile u16* project = stock<const volatile u16*()>(0x8d584210)();
    const u32 tempo = project ? *project : 0;
    return tempo >= 200 && tempo <= 3000 ? tempo : 0;
}

inline u32 audio_frame_index() {
    return *reinterpret_cast<const volatile u16*>(0x84a50b98u);
}

inline u32 track_engine(u32 track) { return stock<u32(u32)>(0x8d58781c)(track); }
// Stock's set: stores a track's parameter word and sends it to the audio side
// (0x8d5acd9c), as it does for every edit.
inline u32 set_parameter(u32 project, u32 track, u32 index, i32 value) {
    return stock<u32(u32, u32, u32, i32)>(0x8d5acf28)(project, track, index, value);
}
inline u32 selected_track() { return stock<u32()>(0x8d4d6f14)(); }
inline bool shift_held() { return stock<u32()>(0x8d51f184)() != 0; }
// Stock's held-key list survives encoder turns; its separate pending-click
// flag does not. A turn while held suppresses the release's click action.
inline bool encoder_held(u32 encoder) {
    return encoder < 4 && stock<u32(u32)>(0x8d5b7d7c)(10 + encoder) != 0;
}
// The stored pad record of a track's key, or null.
inline void* pad_region(u32 track, u32 key) {
    u32 scratch;
    return stock<void*(u32, u32, u32*, u32)>(0x8d58926c)(track, key, &scratch, 0);
}
// The key stock's instrument controller hands its per-key editor (8c0b1344,
// 8c0b1448..8c0b1454): the key it holds for editing at 0x8400dfc8 (a held or
// Shift-selected pad, 8c0b29ce), or, when that is -1 (each page or track change
// sets it so, 8c0b1a04, 8c091d92), the track's last played key (8d4d6cb0, a
// byte at 0x080379f0 + track that every note start sets, sequenced notes too,
// 8d595838), which the turn then latches there (8c0b1678). Stock's dbox page
// shows the same key (8d530724, 8d52d7c8). The track's selected key (8d4d6c98, a
// byte at 0x08037a00 + track) is the last key played from the GUI (8d59429a).
const u32 kDrumEditKey = 0x8400dfc8;
inline u32 drum_held_key() { return word(kDrumEditKey); }
inline u32 drum_played_key(u32 track) { return stock<u32(u32)>(0x8d4d6cb0)(track); }
inline u32 drum_selected_key(u32 track) { return stock<u32(u32)>(0x8d4d6c98)(track); }
// An owned kit's key, which its screen, Shift row and locks show and its
// per-key edits change: the held key, else the selected one. The SDK never
// writes the held key: on an owned kit's track the turn's fallback to the last
// played key (8c0b14b4) gives the selected key instead and its latch
// (8c0b167a) keeps -1 (drum_values_stock.cpp), so only a pad held or
// Shift-selected is held, and a turn edits the key the screen shows.
inline u32 drum_edit_key(u32 track) {
    const u32 held = drum_held_key();
    return held < 128 ? held : drum_selected_key(track);
}
inline void set_text(void* node, const char* text) { stock<void(void*, const char*)>(0x8d6011f0)(node, text); }
inline void* find_node(void* page, const char* id) { return stock<void*(void*, const char*, u32)>(0x8d520308)(page, id, 1); }
}}
