// OS 1.2.1 boundary of the four LFOs' audio evaluation (multi_lfo_audio.hpp), and
// the C entry points of the hook stubs (multi_lfo_hooks.s).
#include "multi_lfo_audio.hpp"

using namespace opxy;
using namespace opxy::platform;

namespace opxy { namespace platform { namespace multi_lfo { namespace stock_lfo {
namespace {
// The audio role's track words (58 per track, 0xe8 bytes apart); stock's one
// LFO save slot per track follows them (8d518ec4, 8d518d36).
const u32 kTrackWords = 0x84eab39c;
const u32 kSlotIndex = kTrackWords + 0x3e80, kSlotOriginal = kTrackWords + 0x3ec0;
const u32 kTypes = 0x11b01dd8;  // u32 per track: the audio role's LFO type
const u32 kOn = 0x11b01e18;     // byte per track: stock's LFO on
}
u32 evaluate(Address state, u32 track, Address words, u32 pass) {
    return stock<u32(Address, u32, Address, u32)>(0x11a0446e)(state, track, words, pass);
}
void reset(Address state, u32 track, Address words) {
    stock<void(Address, u32, Address)>(0x8d5047fe)(state, track, words);
}
void restore(u32 track) { stock<void(u32, u32)>(0x8d518d36)(kTrackWords, track); }
void telemetry(u32 track, u32 index, i32 wave, i32 amount) {
    stock<void(u32, u32, i32, i32)>(0x8d4d68b8)(track, index, wave, amount);
}
bool on(u32 track) { return track < 16 && byte(kOn + track) != 0; }
unsigned char& on_flag(u32 track) { return *at<unsigned char>(kOn + track); }
u32& type(u32 track) { return *at<u32>(kTypes + track * 4); }
i32* words(u32 track) { return at<i32>(kTrackWords + track * 0xe8); }
i32& slot_index(u32 track) { return *at<i32>(kSlotIndex + track * 4); }
i32& slot_original(u32 track) { return *at<i32>(kSlotOriginal + track * 4); }
}}}}

// C entry points: each stub (multi_lfo_hooks.s) passes the stock call's
// arguments; the voice passes their state (voice + 0x98).
extern "C" {
void sdk_multi_lfo_voice0(Address, u32, Address) __asm__("sdk_multi_lfo_voice0");
u32 sdk_multi_lfo_voice1(Address, u32, Address) __asm__("sdk_multi_lfo_voice1");
void sdk_multi_lfo_voice_restore(u32, u32) __asm__("sdk_multi_lfo_voice_restore");
u32 sdk_multi_lfo_track2(Address, u32, Address) __asm__("sdk_multi_lfo_track2");
u32 sdk_multi_lfo_track3(Address, u32, Address) __asm__("sdk_multi_lfo_track3");
u32 sdk_multi_lfo_track4(Address, u32, Address) __asm__("sdk_multi_lfo_track4");
void sdk_multi_lfo_restore_before(u32, u32) __asm__("sdk_multi_lfo_restore_before");
void sdk_multi_lfo_restore_after(u32, u32) __asm__("sdk_multi_lfo_restore_after");
void sdk_multi_lfo_reset(Address, u32, Address) __asm__("sdk_multi_lfo_reset");
void sdk_multi_lfo_telemetry(u32, u32, i32, i32) __asm__("sdk_multi_lfo_telemetry");
}
void sdk_multi_lfo_voice0(Address state, u32 track, Address words) { multi_lfo::voice_pass0(state, track, words); }
u32 sdk_multi_lfo_voice1(Address state, u32 track, Address words) { return multi_lfo::voice_pass1(state, track, words); }
// Call replacements of 8d518d36(0x84eab39c, track).
void sdk_multi_lfo_voice_restore(u32, u32 track) { multi_lfo::voice_restore(track); }
u32 sdk_multi_lfo_track2(Address state, u32 track, Address words) { return multi_lfo::track_pass(state, track, words, 2); }
u32 sdk_multi_lfo_track3(Address state, u32 track, Address words) { return multi_lfo::track_pass(state, track, words, 3); }
u32 sdk_multi_lfo_track4(Address state, u32 track, Address words) { return multi_lfo::track_pass(state, track, words, 4); }
void sdk_multi_lfo_restore_before(u32, u32 track) { multi_lfo::track_restore(track, multi_lfo::track_before); }
void sdk_multi_lfo_restore_after(u32, u32 track) { multi_lfo::track_restore(track, multi_lfo::track_after); }
// Call replacement of 8d5047fe(state, track, words).
void sdk_multi_lfo_reset(Address state, u32 track, Address words) { multi_lfo::reset(state, track, words); }
// Call replacement of 8d4d68b8(track, index, wave, amount).
void sdk_multi_lfo_telemetry(u32 track, u32 index, i32 wave, i32 amount) {
    multi_lfo::telemetry(track, index, wave, amount);
}
