// The drum-kit controls' audio side (SDK 1.23, drum_kit_controls.hpp), which a kit's
// voice adapter (drum_voice.cpp.in) calls at fixed points of every note. A kit
// declaring none of them gets DrumKitVoice<0, ...> and an empty DrumKitState: every
// call is an empty inline function, so its adapter compiles to the code it had
// before. The functions are internal to each adapter's translation unit (an unnamed
// namespace), so two kits never share a definition.
#pragma once
#include <opxy/types.hpp>
#include <opxy/q31.hpp>
#include "drum_kit_controls.hpp"

// Set by the drum renderer's hook just before a voice renders (drum_amp_envelope.cpp):
// that voice has had its note-off. Read only by an ampEnvelope kit's adapter.
extern "C" volatile unsigned char sdk_drum_released __asm__("sdk_drum_released");

namespace opxy { namespace platform {
// A note's amp envelope (ampEnvelope), as stock's drum sampler keeps one: its gain
// (Q31), the blocks since its first render, the blocks it holds (~0: to the end),
// its attack's and fade's per-frame steps (from its key's values, read on its first
// render), whether it has started and whether it is fading out.
struct DrumAmp {
    i32 level;
    u32 blocks, hold;
    i32 rise, fall;
    u8 started, fading, unused[2];
};
// A voice's state for the controls: fresh, 1 until the note's first render, or 2
// while a note that cut its pad's sounding voice waits out that voice's fade
// (ampEnvelope with chokeSameKey); volume_gain, the pad gain its last block ended on
// (Q27; 0 before its first); jitter, its random offsets of the eight controls.
template <u32 Controls> struct DrumKitState {
    u32 fresh;
    i32 volume_gain;
    i8 jitter[8];
    DrumAmp amp;
};
template <> struct DrumKitState<0> {};

namespace {

// Stock's drum sampler step tables, read in place (11a07dea..e56, 11a07e1c..e60).
const i32* const kKitAttackSteps = reinterpret_cast<const i32*>(0x8404650c);
const i32* const kKitFadeSteps = reinterpret_cast<const i32*>(0x840512e4);

template <u32 Controls, bool Choke> struct DrumKitVoice {
    typedef DrumKitState<Controls> State;
    static const bool volume = (Controls & kKitVolume) != 0, random = (Controls & kKitRandom) != 0,
                      amp = (Controls & kKitAmp) != 0;
    // With ampEnvelope a same-key cut lands between blocks: the cut voice fades over
    // one block (64 frames, 1.45 ms; the fade's Q31 step is 1 << 25) and the note that
    // cut it starts with the next block, so the two never render in the same block.
    // Every other kit's cut fades over 128 frames under the new note.
    static const bool defer = Choke && amp;
    static const u32 choke_shift = defer ? 25 : 24;

    // The SDK's own generator (xorshift32), audio side only.
    static u32 draw() {
        static u32 noise = 0x9e3779b9u;
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return noise;
    }
    // A note's start; `cut`: it cut a voice still sounding its pad.
    static void start(State& s, bool cut) {
        s.fresh = defer && cut ? 2 : random || defer ? 1 : 0;
        s.volume_gain = 0;
        s.amp = DrumAmp();
    }
    // Before a render: 0 to render; 1 to render silence (a note waiting out its pad's
    // cut voice), 2 to render silence and free the voice (cut itself while waiting,
    // `fading`, it never sounds).
    static u32 waiting(State& s, bool fading) {
        if (!defer || s.fresh < 2) return 0;
        s.fresh = 1;
        return fading ? 2 : 1;
    }
    // The note's first render: its random offsets, drawn from its key's random (a key
    // the kit has marked; any other plays without one).
    static void first(State& s, const i32* region) {
        if (!s.fresh) return;
        s.fresh = 0;
        if (!random) return;
        const i32 word = region && drum_marked_q31(region[0]) ? region[kDrumPadPayload] : 0;
        const i32 depth = word <= 0 ? 0 : ((word >> 16) * kKitRandomReach) >> 15;
        for (u32 i = 0; i < 8; ++i)
            s.jitter[i] = static_cast<i8>((static_cast<i32>(static_cast<i16>(draw() >> 16)) * depth) >> 15);
    }
    static bool unpack(const i32* words, u8* model, u8* controls, const DrumCodec& codec) {
        return amp ? kit_amp_unpack(words, model, controls, codec) : drum_unpack(words, model, controls, codec);
    }
    // The values the note plays: its offsets added, after the track's.
    static void played(const State& s, u8* values) {
        if (!random) return;
        for (u32 i = 0; i < 8; ++i) {
            const i32 value = static_cast<i32>(values[i]) + s.jitter[i];
            values[i] = static_cast<u8>(value < 0 ? 0 : value > 127 ? 127 : value);
        }
    }
    // A step table's index for a word, as the drum sampler computes it: 0..63.
    static u32 amp_index(i32 word) {
        const i32 rest = 0x7fffffff - (word < 0 ? 0 : word);
        return static_cast<u32>(0x7fffffff - q31::multiply(q31::multiply(rest, rest), 0x7fffffff)) >> 25;
    }
    // The note's envelope from its key's values (payload words 2..4: attack, hold and
    // decay; the start, where a sample would begin, has no use without one), or the
    // kit's defaults for a key the kit has not marked or a value it has not stored.
    static void amp_draw(DrumAmp& e, const i32* region, const i32* defaults) {
        const bool keyed = region && drum_marked_q31(region[0]);
        const i32 attack = keyed ? kit_amp(region[2], defaults[1]) : defaults[1];
        const i32 hold = keyed ? kit_amp(region[3], defaults[2]) : defaults[2];
        const i32 decay = keyed ? kit_amp(region[4], defaults[3]) : defaults[3];
        const u32 rise = amp_index(attack);
        e.started = 1;
        e.level = rise ? 0 : 0x7fffffff;
        e.rise = kKitAttackSteps[rise];
        e.fall = -kKitFadeSteps[amp_index(decay)];
        // Hold: the fade-out begins hold x 2 s (1378 blocks) after the attack has reached
        // full scale; at full, never.
        e.hold = hold < 0x7fff0000 ? (static_cast<u32>(hold < 0 ? 0 : hold) >> 16) * 1378u >> 15 : ~0u;
    }
    // The amplitude pass: false leaves it to the adapter's own. With ampEnvelope, in
    // a block the envelope shapes (attack not over, or fading out), each frame's
    // product with the gain vector times the level, which then steps, staying at or
    // above zero; the fade begins at the hold point, counted once the attack is over (so
    // a hold of 0 still lets the attack finish), or at the note-off. The note-off is
    // the hook's for this voice's render (drum_amp_envelope.cpp), taken and cleared here
    // so no later render reads it.
    static bool shape(State& s, i32* output, const i32* amplitude, const i32* region, const i32* defaults) {
        if (!amp) return false;
        DrumAmp& e = s.amp;
        if (!e.started) amp_draw(e, region, defaults);
        const bool released = sdk_drum_released;
        sdk_drum_released = 0;
        const bool full = e.level == 0x7fffffff;
        if (released || (full && e.blocks >= e.hold)) e.fading = 1;
        else if (full && e.blocks < 0xffffffffu) ++e.blocks;
        const i32 step = e.fading ? e.fall : e.level == 0x7fffffff ? 0 : e.rise;
        if (!step) return false;
        i32 level = e.level;
        for (u32 i = 0; i < block_frames; ++i) {
            output[i] = q31::multiply(q31::multiply(output[i], amplitude[i]), level);
            level = q31::add(level, step);
            if (level < 0) level = 0;
        }
        e.level = level;
        return true;
    }
    // Q31 x Q27 is the sample over 16: scaled back by 16, saturating.
    static i32 pad_scale(i32 sample, i32 gain) {
        const i32 scaled = q31::multiply(sample, gain);
        return scaled > 0x07ffffff ? 0x7fffffff : scaled < -0x08000000 ? static_cast<i32>(0x80000000u)
                                                                       : static_cast<i32>(static_cast<u32>(scaled) << 4);
    }
    // After the amplitude: true when the note's envelope has faded it to silence, to be
    // freed once this block is mixed. With padVolume, the key's gain (payload word 7,
    // Q27; a key the kit has not marked plays at 0 dB), ramped over the block from the
    // gain the last block ended on so a turn while a note sounds does not click; the
    // note's first block starts at its gain, a gain the last block also ended on scales
    // the block directly, and unity leaves it untouched. Above unity the product
    // saturates.
    static bool after(State& s, i32* output, const i32* region) {
        if (amp && s.amp.fading && !s.amp.level) return true;
        if (!volume) return false;
        const i32 unity = kDrumUnityGain;
        const i32 word = region && drum_marked_q31(region[0]) ? region[kDrumGainWord] : unity;
        const i32 target = word < 0 ? 0 : word;
        const i32 from = s.volume_gain ? s.volume_gain : target;
        s.volume_gain = target ? target : 1;   // 0 marks a note's first block; a silent key stays silent
        if (from == unity && target == unity) return false;
        if (from == target) {
            for (u32 i = 0; i < block_frames; ++i) output[i] = pad_scale(output[i], target);
            return false;
        }
        static_assert(block_frames == 64, "The gain ramp divides by 64 frames");
        const i32 slope = (target - from) >> 6;
        i32 gain = from;
        for (u32 i = 0; i < block_frames; ++i) {
            gain += slope;
            output[i] = pad_scale(output[i], i + 1 == block_frames ? target : gain);
        }
        return false;
    }
    // A note that has not rendered yet (or still waits out its pad's cut voice) is
    // active, and its engine is not asked whether it has finished.
    static bool unsounded(const State& s) { return (random || defer) && s.fresh; }
};
// A kit without the controls: nothing.
template <bool Choke> struct DrumKitVoice<0, Choke> {
    typedef DrumKitState<0> State;
    static const u32 choke_shift = 24;
    static void start(State&, bool) {}
    static u32 waiting(State&, bool) { return 0; }
    static void first(State&, const i32*) {}
    static bool unpack(const i32* words, u8* model, u8* controls, const DrumCodec& codec) {
        return drum_unpack(words, model, controls, codec);
    }
    static void played(const State&, u8*) {}
    static bool shape(State&, i32*, const i32*, const i32*, const i32*) { return false; }
    static bool after(State&, i32*, const i32*) { return false; }
    static bool unsounded(const State&) { return false; }
};
}}}
