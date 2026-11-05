// Per-key sends (drum_send.hpp): the C++ side of the stock mixer hooks in
// drum_send_hooks.s. Audio role only; nothing here allocates or waits.
#include "drum_send.hpp"
#include "drumline.hpp"

using opxy::i32;
using opxy::u32;
using namespace opxy::platform;

extern "C" {
void sdk_drum_send_begin() __asm__("sdk_drum_send_begin");
void sdk_drum_send_voice(const unsigned char* voice, u32 track) __asm__("sdk_drum_send_voice");
void sdk_drum_send_own_mix(u32 track, i32 level) __asm__("sdk_drum_send_own_mix");
// Read by sdk_drum_send_source_1 and _2: per send, the track plus one and its source.
extern DrumSendState sdk_drum_send_state __asm__("sdk_drum_send_state");
}

DrumSendState sdk_drum_send_state;
opxy::u8 sdk_drum_send_dbox[kDboxVoices];

namespace {
// Per send, the followers' sum and the own-amount sum. One set is enough: the
// engine (8d510760) mixes its tracks one at a time, each voice loop (8d51c708,
// called only at 8d510bea) followed by that track's sends.
i32 follow[kDrumSends][kDrumSendWords];
i32 own[kDrumSends][kDrumSendWords];
// Stock's stereo sum of the track's voices, which the engine hands the voice
// loop (8d510be2) and its send loops read (8d511088, 8d5110ca).
// The address is the immediate of the instructions the hooks replace, which
// tests/test_drum_send.py decodes from the profile's bytes: 0x11900000, a
// misread of it, is another stock buffer (set up at 8d4d246a), which put
// the live input into the sends in an early hardware test.
i32* const kTrackSum = reinterpret_cast<i32*>(0x11903600);
// The voice just rendered, as the loop adds it into the sum (8d51c7b2).
const i32* const kVoiceOutput = reinterpret_cast<const i32*>(0x11b01548);
// The send buses stock's loops write, FX I's (8d511096) and FX II's (8d5110da),
// which 8d50f594 then hands send slots 14 and 15 (8d5116a0, 8d511908).
i32* const kSendBuses[kDrumSends] = {reinterpret_cast<i32*>(0x84caa140), reinterpret_cast<i32*>(0x84caa340)};
// The stock voice's machine object, whose method table stock calls (8d51b53e).
const u32 kVoiceObject = 0x94;

u32 codes_of(const unsigned char* voice) {
    const void* object = *reinterpret_cast<const void* const*>(voice + kVoiceObject);
    if (!object) return 0;
    const void* table = *static_cast<const void* const*>(object);
    // A stock Drumline voice: the codes its render kept (drumline.cpp, the
    // drumlinePage2 option; 0 without it). Only stock's dbox factory makes an
    // object with this method table, so no kit's voice reads here.
    if (reinterpret_cast<__UINTPTR_TYPE__>(table) == kDboxMethods) {
        const u32 index = dbox_index(static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(object)));
        return index < kDboxVoices ? sdk_drum_send_dbox[index] : 0;
    }
    for (u32 i = 0; i < sdk_drum_send_kit_count; ++i)
        if (sdk_drum_send_kits[i].vtable == table) return sdk_drum_send_kits[i].codes(object);
    return 0;
}
}

// 8d510bea: a track's voice loop is about to run.
void sdk_drum_send_begin() { drum_send_begin(sdk_drum_send_state); }

// 8d51c7b2: before stock adds `voice` (its rendered block at 0x11b01548) into
// the sum of `track`.
void sdk_drum_send_voice(const unsigned char* voice, u32 track) {
    drum_send_voice(sdk_drum_send_state, follow, own, track, kTrackSum, kVoiceOutput, codes_of(voice));
}

// 8d511116: stock's send loops for `track` are done; `level` is the engine's
// gain for the track's dry and sends alike ([FP-0x68], 8d510d38).
void sdk_drum_send_own_mix(u32 track, i32 level) {
    drum_send_own(sdk_drum_send_state, own, kSendBuses, track, level);
}
