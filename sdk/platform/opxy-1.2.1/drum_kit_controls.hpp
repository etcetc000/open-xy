// The drum-kit controls (SDK 1.23): resources.padVolume, padRandom and ampEnvelope,
// each asked for on its own by a kit without padMix. What they store in a key's pad
// record, the build's table of kits declaring them (sdk_drum_kits, generated into
// drum_callbacks.cpp only for a build with such a kit) and the GUI entry points
// drum_kit_controls.cpp defines (the kit_controls profile service).
//
// The shared GUI units reach those entry points only through calls compiled with
// OPXY_KIT_CONTROLS, which native.py defines for every SDK unit of a build with such
// a kit; every other build compiles them as before. Each call answers for the kit it
// is given, so a kit without the controls plays and edits as it does without them.
// A browser selection of such a build that leaves every such kit out resolves the
// entry points to kit_absent, which answers 0: a kit without the controls
// (native.service_fallbacks). A kit's voice adapter takes the audio side from
// drum_kit_voice.hpp.
#pragma once
#include <opxy/types.hpp>
#include "drum_machines.hpp"
#include "drum_storage.hpp"

namespace opxy { namespace platform {
// A kit's choice of them, one bit each.
const u32 kKitVolume = 1, kKitRandom = 2, kKitAmp = 4;

// padVolume: the key's level, stock's signed whole-dB byte at +0x1d
// (drum_storage.hpp kDrumLevel), -30..+20 in 1 dB steps as stock's own pad
// editor offers it (8d4d60d8), exported by presets as the region's `gain`. Stock's
// payload builder puts it in word 7 as Q27 with 0 dB unity (kDrumGainWord). As a
// Shift choice, dB + 30: 0..50, 30 the unity.
const i32 kKitVolumeFloor = -30, kKitVolumeCeiling = 20;
static inline i32 kit_volume(i32 db) {
    return db < kKitVolumeFloor ? kKitVolumeFloor : db > kKitVolumeCeiling ? kKitVolumeCeiling : db;
}
static inline u32 kit_volume_choice(i32 db) { return static_cast<u32>(kit_volume(db) - kKitVolumeFloor); }

// padRandom: the key's random, stock Drumline's pad word +0x14 (kDrumPadWord) as
// Q31, set in 1% steps and exported by presets as the region's `random` (word >> 16).
// Stock delivers max(word, 0) as payload word 6. As a Shift choice, its percent.
const i32 kKitRandomStep = 0x7fffffff / 100;
static inline u32 kit_random_choice(i32 word) {
    return word <= 0 ? 0 : (static_cast<u32>(word >> 16) * 100 + 16383) >> 15;
}
// How far a full random moves each control of a hit, in 0..127 steps either way.
const i32 kKitRandomReach = 32;

// ampEnvelope: a key's amp envelope, M2's start, attack, hold and decay (track
// words 8..11), one value in the low half of each of its four parameter words as
// its top 15 bits plus one; 0 is unset and reads as the kit's default. Projects
// save the pad record raw (8d598e68) and stock's pad and track copies move it whole
// (8d581e50), so the values follow the key; a preset keeps the top halves only
// (>> 16 out, << 16 in), and its keys play the kit's defaults. Stock copies the
// words whole into payload words 1..4 (8d5ad70a..718). drum_pack leaves the low
// halves zero and drum_unpack refuses others, so an ampEnvelope kit's keys are read
// through kit_amp_unpack and kept across a pack (drum_kit_keep, drum_kit_restore).
const u32 kKitAmpValues = 4;
static_assert(kKitAmpValues == kDrumStorageWords, "Each stored word keeps one amp value");
static inline i32 kit_amp(i32 word, i32 fallback) {
    const u32 low = static_cast<u32>(word) & 65535u;
    return low && low <= 32768u ? static_cast<i32>((low - 1) << 16) : fallback;
}
static inline i32 kit_amp_store(i32 word, i32 value) {
    const u32 q = value < 0 ? 0u : static_cast<u32>(value) >> 16;
    return static_cast<i32>((static_cast<u32>(word) & 0xffff0000u) | (q + 1));
}
// The stored words' top halves, when every low half is an amp value (0..32768);
// false for any other (a stock engine's full Q31 values), leaving tops untouched.
static inline bool kit_amp_tops(const i32* words, i32* tops) {
    if (!words) return false;
    for (u32 i = 0; i < kDrumStorageWords; ++i)
        if ((static_cast<u32>(words[i]) & 65535u) > 32768u) return false;
    for (u32 i = 0; i < kDrumStorageWords; ++i) tops[i] = static_cast<i32>(static_cast<u32>(words[i]) & 0xffff0000u);
    return true;
}
static inline bool kit_amp_unpack(const i32* words, u8* model, u8* controls, const DrumCodec& codec) {
    i32 tops[kDrumStorageWords];
    return kit_amp_tops(words, tops) && drum_unpack(tops, model, controls, codec);
}

// A kit declaring any of them: its engine, its bits and, with ampEnvelope, its
// default amp words 8..11 (its init preset's amp attack, decay, sustain and release
// << 16, which M2 names start, attack, hold and decay).
struct DrumKit {
    const char* id;
    u32 controls;
    const i32* amp;
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
    i32 amp_default(u32 i) const { return amp && i < kKitAmpValues ? amp[i] : 0; }
    // What Shift and the encoder at a position (0..3) set beside DrumMachine::shift:
    // the key's volume on the third (with groups in the choke group's place), its
    // random on the fourth; 0 for a position the kit leaves to DrumMachine::shift.
    u32 shift(u32 position) const {
        if (position == 3 && (controls & kKitRandom)) return kKitRandom;
        if (position == 2 && (controls & kKitVolume)) return kKitVolume;
        return 0;
    }
};
}}

extern "C" {
extern const opxy::platform::DrumKit sdk_drum_kits[] __asm__("sdk_drum_kits");
extern const opxy::u32 sdk_drum_kit_count __asm__("sdk_drum_kit_count");
// A kit's row, or null for a kit without the controls.
const opxy::platform::DrumKit* sdk_drum_kit(opxy::u32 engine) __asm__("sdk_drum_kit");
// drum_values.hpp drum_region_values: an ampEnvelope kit's key, unpacked past its amp
// values when marked (else left at its defaults), and true; false for any other kit.
bool sdk_drum_kit_unpack(const opxy::platform::DrumMachine& machine, const void* region, opxy::u8* model,
                         opxy::u8* controls) __asm__("sdk_drum_kit_unpack");
// Whether a pack of this key keeps its low halves: an ampEnvelope kit's key it has
// marked, whose halves are amp values.
bool sdk_drum_kit_keeps(const opxy::platform::DrumMachine& machine, const void* region) __asm__("sdk_drum_kit_keeps");
// A key a kit with the controls marks for the first time (an edit, a scramble): its
// level 0 dB (padVolume) and its random none (padRandom), as it played unmarked.
void sdk_drum_kit_setup(const opxy::platform::DrumMachine& machine, void* region) __asm__("sdk_drum_kit_setup");
// sdk_drum_edit's Shift turn at a position (0..3): true when the kit sets something
// there (the key's volume or random), which it then edited and published.
bool sdk_drum_kit_edit(const opxy::platform::DrumMachine& machine, opxy::u32 track, opxy::u32 key, void* region,
                       opxy::u32 position, opxy::i32 delta, opxy::u8 model, const opxy::u8* controls)
    __asm__("sdk_drum_kit_edit");
// A kit screen's Shift cell at a position (0..3): true when the kit sets something
// there, with its choice (the key's volume as dB + 30, or its random's percent), its
// title and its value's text (16 bytes).
bool sdk_drum_kit_readout(opxy::u32 track, opxy::u32 position, opxy::u8* choice, const char** title, char* value)
    __asm__("sdk_drum_kit_readout");
// Once a GUI frame (sdk_sample_gui_tick): M2's amp words follow the selected key of
// an ampEnvelope kit, and M2's edits go to that key.
void sdk_drum_amp_tick() __asm__("sdk_drum_amp_tick");
}

namespace opxy { namespace platform {
// A pack clears the stored words' low halves: an ampEnvelope kit's amp values, kept
// around it. A key the kit never stored may hold other halves from stock, which the
// pack then clears, so the key reads back as the kit's.
struct DrumKitKept {
    bool kept;
    i32 low[kKitAmpValues];
};
static inline DrumKitKept drum_kit_keep(const DrumMachine& machine, const void* region) {
    DrumKitKept kept;
    kept.kept = sdk_drum_kit_keeps(machine, region);
    for (u32 i = 0; i < kKitAmpValues; ++i) kept.low[i] = kept.kept ? static_cast<const i32*>(region)[1 + i] : 0;
    return kept;
}
static inline void drum_kit_restore(const DrumKitKept& kept, void* region) {
    if (!kept.kept) return;
    i32* words = static_cast<i32*>(region) + 1;
    for (u32 i = 0; i < kKitAmpValues; ++i)
        words[i] = static_cast<i32>((static_cast<u32>(words[i]) & 0xffff0000u) | (static_cast<u32>(kept.low[i]) & 65535u));
}
}}
