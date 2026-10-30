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

inline u32 track_engine(u32 track) { return stock<u32(u32)>(0x8d58781c)(track); }
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
inline void set_text(void* node, const char* text) { stock<void(void*, const char*)>(0x8d6011f0)(node, text); }
inline void* find_node(void* page, const char* id) { return stock<void*(void*, const char*, u32)>(0x8d520308)(page, id, 1); }
}}
