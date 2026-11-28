// Stock's mixer (mixer_hooks.s, and track_call.s with a track stage), service mixer: four sites shared by the per-key sends
// (drum_send.cpp, service drum_sends) and Sample Studio's player (sample_studio_audio.cpp,
// service sample_studio). A build with only one of them resolves the other's entry points to
// kit_absent, which does nothing (tooling/native.py service_fallbacks). Audio role only.
#include <opxy/types.hpp>

using opxy::i32;
using opxy::u32;

extern "C" {
void sdk_mixer_begin() __asm__("sdk_mixer_begin");
void sdk_mixer_voice(const unsigned char* voice, u32 track) __asm__("sdk_mixer_voice");
void sdk_mixer_gain(u32 track, i32 gain, i32 step) __asm__("sdk_mixer_gain");
void sdk_mixer_own(u32 track, i32 level, const unsigned char* frame) __asm__("sdk_mixer_own");
// The per-key sends.
void sdk_drum_send_begin() __asm__("sdk_drum_send_begin");
void sdk_drum_send_voice(const unsigned char* voice, u32 track) __asm__("sdk_drum_send_voice");
void sdk_drum_send_gain(u32 track, i32 gain, i32 step) __asm__("sdk_drum_send_gain");
void sdk_drum_send_own_mix(u32 track, i32 level, const unsigned char* frame) __asm__("sdk_drum_send_own_mix");
// Sample Studio's player: silences the voices of the track the studio has open.
void sdk_studio_mixer_begin() __asm__("sdk_studio_mixer_begin");
void sdk_studio_voice(i32* out, u32 track) __asm__("sdk_studio_voice");
void sdk_studio_gain(u32 track, i32 gain) __asm__("sdk_studio_gain");
void sdk_studio_level(u32 track, i32 level) __asm__("sdk_studio_level");
}

namespace {
// The voice just rendered, as the loop adds it into the track's sum (8d51c7b2).
i32* const kVoiceOutput = reinterpret_cast<i32*>(0x11b01548);
}

// 8d510bea: a track's voice loop is about to run.
void sdk_mixer_begin() {
    sdk_studio_mixer_begin();
    sdk_drum_send_begin();
}
// 8d51c7b2: before stock adds `voice` (its rendered block at 0x11b01548) into the sum of `track`.
void sdk_mixer_voice(const unsigned char* voice, u32 track) {
    sdk_studio_voice(kVoiceOutput, track);
    sdk_drum_send_voice(voice, track);
}
// 8d510ea8: stock has just scaled the sum of `track` by the track's gain.
void sdk_mixer_gain(u32 track, i32 gain, i32 step) {
    sdk_studio_gain(track, gain);
    sdk_drum_send_gain(track, gain, step);
}
// 8d511116: stock's send loops for `track` are done and its dry loop is next.
void sdk_mixer_own(u32 track, i32 level, const unsigned char* frame) {
    sdk_studio_level(track, level);
    sdk_drum_send_own_mix(track, level, frame);
}
