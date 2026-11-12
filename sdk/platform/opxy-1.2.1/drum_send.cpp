// Per-key sends (drum_send.hpp): the C++ side of the stock mixer hooks in
// drum_send_hooks.s. Audio role only; nothing here allocates or waits.
#include "drum_send.hpp"
#include "drumline.hpp"

using opxy::i32;
using opxy::u32;
using namespace opxy::platform;
namespace q31 = opxy::q31;

extern "C" {
void sdk_drum_send_begin() __asm__("sdk_drum_send_begin");
void sdk_drum_send_voice(const unsigned char* voice, u32 track) __asm__("sdk_drum_send_voice");
void sdk_drum_send_gain(u32 track, i32 gain, i32 step) __asm__("sdk_drum_send_gain");
void sdk_drum_send_own_mix(u32 track, i32 level, const unsigned char* frame) __asm__("sdk_drum_send_own_mix");
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
// Per send, the own-amount voices as they are, for their dry (drum_send_dry),
// and each track's smoothed dry gains.
i32 raw[kDrumSends][kDrumSendWords];
DrumSendDry dry[kDrumSendTracks];
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
// The effect objects of send slots 14 and 15 (0x11b01d88 + 4 * slot), which the
// engine reads for their dry queries (8d510cfe..8d510d0e, 8d510d62..8d510d72),
// and every track's record (0x84eab39c + 0xe8 * track, 8d518d94).
void* const* const kSendEffects = reinterpret_cast<void* const*>(0x11b01dc0);
const unsigned char* const kRecords = reinterpret_cast<const unsigned char*>(0x84eab39c);
const u32 kRecordBytes = 0xe8, kFx2Knob = 0x5c, kDryMethod = 7;
// The engine's dry word per track (its argument 0x84c696fc + 4 + 4 * track:
// 8d50e6f6..8d50e71c set all sixteen to 0x7fffffff; 8d511122 reads the track's).
const i32* const kDryWords = reinterpret_cast<const i32*>(0x84c69700);
// In the engine's frame (8d510760: LINK 0xb4, seven registers pushed, SP += -0x14,
// so its SP is FP - 0xe4): FX I's gain [FP-0x8] (8d510db6, stored back by its
// loop at 8d5110c2 with a step of 0), the dry smoother [FP-0x34] (8d51111a) and
// the main out [SP+0x58] (8d51114a, the engine's second argument, 8d510772).
const int kFrameFx1 = -0x8, kFrameDrySmoother = -0x34, kFrameMainOut = -0xe4 + 0x58;
template <typename T> T frame_word(const unsigned char* frame, int offset) {
    T value;
    __builtin_memcpy(&value, frame + offset, sizeof value);
    return value;
}

// What the effect on send slot 14 + `s` answers to its dry query, asked as the
// engine asks it (8d510d3a..8d510d5e, 8d510dd0..8d510df4): method word 7 of the
// slot's object, given the object and the send track's record; 0x7fffffff, as
// the engine takes it, for an empty slot.
i32 dry_query(u32 s) {
    void* object = kSendEffects[s];
    if (!object) return q31::maximum;
    typedef i32 (*Query)(void*, const void*);
    const Query query = reinterpret_cast<Query>((*static_cast<void* const* const*>(object))[kDryMethod]);
    return query(object, kRecords + (14 + s) * kRecordBytes);
}

u32 codes_of(const unsigned char* voice) {
    const void* object = *reinterpret_cast<const void* const*>(voice + kVoiceObject);
    if (!object) return 0;
    const void* table = *static_cast<const void* const*>(object);
    // A stock Drumline voice: the codes its render kept (drumline.cpp, with a
    // drumline_page extension; 0 without one). Only stock's dbox factory makes an
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
    drum_send_voice(sdk_drum_send_state, follow, own, raw, track, kTrackSum, kVoiceOutput, codes_of(voice));
}

// 8d510ea8: stock has just scaled the sum of `track` in place by the track's
// gain (8d510e60..8d510e98); `gain` is where its ramp ended (R0), `step` its
// step per frame (R2).
void sdk_drum_send_gain(u32 track, i32 gain, i32 step) {
    drum_send_track_gain(sdk_drum_send_state, follow, own, raw, track, gain, step);
}

// 8d511116: stock's send loops for `track` are done and its dry loop is next;
// `level` is the engine's gain for the track's dry and sends alike ([FP-0x68],
// 8d510d38), `frame` the engine's frame pointer.
void sdk_drum_send_own_mix(u32 track, i32 level, const unsigned char* frame) {
    DrumSendState& state = sdk_drum_send_state;
    drum_send_own(state, own, kSendBuses, track, level);
    if (track >= kDrumSendTracks) return;
    if (state.own[0] != track + 1 && state.own[1] != track + 1) {
        drum_send_dry_idle(dry[track]);
        return;
    }
    DrumSendDryInput in;
    in.level = level;
    in.send[0] = frame_word<i32>(frame, kFrameFx1);
    // FX II's gain as 8d510e0a makes it: the level times the track's knob (word 23).
    in.send[1] = q31::multiply(level, frame_word<i32>(kRecords + track * kRecordBytes, kFx2Knob));
    for (u32 s = 0; s < kDrumSends; ++s)
        in.wet[s] = state.own[s] == track + 1 ? drum_send_subtract(q31::maximum, dry_query(s)) : 0;
    in.x = kDryWords[track];
    const unsigned char* smoother = frame_word<const unsigned char*>(frame, kFrameDrySmoother);
    in.shift = frame_word<short>(smoother, 8);
    in.reset = smoother[0xa] != 0;
    drum_send_dry(dry[track], state, raw, own, frame_word<i32*>(frame, kFrameMainOut), track, in);
}
