// Stock drumline voices inside a synth machine (resources.stockVoice "drumline").
//
// The machine owns each drumline voice and its whole patch; the user's own
// stock OS renders it. The SDK only calls stock's drumline code at fixed,
// profile-pinned addresses in the firmware the build patches: no stock code or
// data is part of a machine or of this SDK. Host builds (machine tests, the
// browser preview and the CPU estimate) have no firmware: voices there are
// silent and cost nothing, so judge sound and CPU on the device.
#pragma once
#include <opxy/audio.hpp>
#include <opxy/q31.hpp>

namespace opxy { namespace stock_drumline {

// Stock's model families, in stock order: kick, snare, hihat, shaker, clap,
// congas, fm, fm slap, pitch, tambourine, resonator.
const u32 models = 11;

// Stock's voice object and the binding's note state, opaque. Put it in the
// machine's Voice, word aligned.
struct Voice {
    u32 words[140];
    u32 held;
    void* slot;
    const void* handle;
    i32 release;
    i32 hold;  // blocks of the quiet hold played since key-up (release_hold_blocks)
    u32 release_order;  // which gate release this was (the binding's count of them)
    // Synced loop. Times are samples, Q8, wrapping (`clock` at the end of the last
    // block, `struck` when the hit now sounding began to be heard);
    // the hit's peak level and its level at the end of the last block (log2, Q8);
    // when it fell to the loop's level (samples into the hit, 0 not yet); the
    // division it last repeated at (index plus one, 0 none); its repeat time
    // (samples, 0 not yet known), `exact` once measured for the sound named by
    // `sound` (a sum of the patch and envelope words it depends on), else carried
    // on from each hit's fall.
    u32 clock, struck;
    i32 peak_level, last_level;
    u32 peak_at;  // when it peaked (samples into the hit, stock's clock)
    u32 natural, division, measured, exact, sound;
    u32 note;  // key mode's step it last repeated at (key_step's code, 0 none)
    // A restart lands on its exact sample, fractions included: the hit's output is
    // delayed by `delay` samples (Q8, under a block), `prior_delay` that of the
    // stream `pending` holds (the block rendered before this one); `fresh` while
    // that stream is the hit just restarted from.
    u32 delay, prior_delay, fresh;
    i32 pending[64];
};

// The track's first envelope word, M2's knob 1: three choices, by thirds of its
// range, as stock's editor steps them (bucket centres 1/6, 1/2, 5/6): one-shot;
// loop, a held note re-hitting; and tempo_loop, looping at the tempo division
// nearest the hit's own repeat time.
const i32 loop_choices = 3, tempo_loop = 0x6aaaaaaa;
inline bool looping(i32 word) { return word >= 0x2aaaaaab; }
inline bool tempo_synced(i32 word) { return word >= 0x55555555; }

// The tempo loop's divisions, in 96ths of a quarter note: the arp's 1/64 to 1/4
// with triplets, then on to 4 bars so a long hit snaps rather than is cut short.
const u32 tempo_divisions = 15;
const u16 division_ticks[tempo_divisions] = {6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 768, 1536};
// Where one division gives way to the next: their geometric mean, in 16ths of a tick.
const u16 division_bounds[tempo_divisions - 1] = {111, 157, 222, 314, 443, 627, 887, 1254,
                                                  1774, 2508, 3547, 5017, 8689, 17378};
// The division (index) nearest a repeat time in 16ths of a tick. The one it
// repeated at last (index plus one, 0 none) holds until the time is a 16th past
// either of its own bounds, so a hit near a bound doesn't alternate; other bounds
// stay where they are.
inline u32 nearest_division(u32 ticks16, u32 previous) {
    u32 k = 0;
    while (k + 1 < tempo_divisions) {
        u32 bound = division_bounds[k];
        if (previous && previous - 1 == k) bound += bound / 16;      // just above it
        else if (previous && previous - 1 == k + 1) bound -= bound / 16;  // just below it
        if (ticks16 <= bound) break;
        ++k;
    }
    return k;
}
// n / d, rounded down; 0 for d 0. Long division a bit at a time, always 32 steps,
// each choosing its remainder without a branch, so its cost doesn't depend on n or d.
inline u32 quotient(u32 n, u32 d) {
    if (!d) return 0;
    u32 q = 0, r = 0;
    for (int bit = 31; bit >= 0; --bit) {
        r = r << 1 | (n >> bit & 1);
        const u32 fits = r >= d;
        r -= d & (0u - fits);
        q = q << 1 | fits;
    }
    return q;
}
// A division's length in samples at 44.1 kHz and bpm_x10 tenths of a BPM:
// 44100 * 60 * 10 / 96 = 275625 samples per tick at 1 BPM x10.
inline u32 division_samples(u32 index, u32 bpm_x10) { return quotient(division_ticks[index] * 275625u, bpm_x10); }
// The same with its fraction: samples, Q8, so repeats don't drift from the tempo.
inline u32 division_period(u32 index, u32 bpm_x10) {
    if (!bpm_x10) return 0;
    const u32 n = division_ticks[index] * 275625u, whole = quotient(n, bpm_x10);
    return whole << 8 | quotient((n - whole * bpm_x10) << 8, bpm_x10);
}
// Repeat times shorter than this (16ths of a tick: between 1/64 and 1/96) snap to
// the played key instead: its longest octave under 1/64, an octave up for each
// octave shorter the time is, so the tone follows the keys.
const u32 note_sync_bound = 78;
// The synth path's phase step a sample at C4 (2^32 a cycle, 44.1 kHz).
const u32 c4_increment = 25480119;
// n / d (both under 2^31), Q16: whole part above, fraction below.
inline u32 ratio_q16(u32 n, u32 d) {
    if (!d) return 1u << 16;
    const u32 whole = quotient(n, d);
    u32 rest = n - whole * d, fraction = 0;
    for (int bit = 15; bit >= 0; --bit) {
        rest <<= 1;
        if (rest >= d) {
            rest -= d;
            fraction |= 1u << bit;
        }
    }
    return whole << 16 | fraction;
}
// x * r for r from ratio_q16 (x under 2^24, r's whole part under 2^7).
inline u32 scale_q16(u32 x, u32 r) {
    return x * (r >> 16) + static_cast<u32>(q31::multiply(static_cast<i32>(x), static_cast<i32>((r & 0xffff) << 15)));
}
// The played key's own period (samples at 44.1 kHz, Q8) from its phase step a
// sample (2^32 a cycle): C4's, 2^40 / c4_increment, moved by the key's interval.
const u32 c4_period = 43151;
inline u32 key_period(u32 increment) {
    const u32 key = increment >= (c4_increment >> 6) && increment < (1u << 30) ? increment : c4_increment;
    return scale_q16(c4_period, ratio_q16(c4_increment, key));  // keys over C-2: under 2^22
}
// Key mode: 1/64's bound at the tempo (samples, Q8); the key's longest octave
// under it, "key"; the knob's step, an octave up for each octave its time is under
// the bound (key_octave the first, by time alone, so every key takes the same step;
// at least a block for every key); and the step's period for the key.
const u32 key_octave = 64;
inline u32 key_bound(u32 bpm_x10) { return bpm_x10 ? quotient(note_sync_bound * 275625u * 16u, bpm_x10) : 0; }
inline u32 key_root(u32 key, u32 bound) {
    if (!key || !bound) return 0;
    u32 period = key;
    while (period >= bound) period >>= 1;
    while ((period << 1) < bound) period <<= 1;
    return period;
}
inline u32 key_step(u32 target, u32 bound, u32 previous) {
    if (!target || !bound) return 0;
    if (previous >= key_octave && previous - key_octave < 24) {
        const u32 hi = bound >> (previous - key_octave), lo = hi >> 1;  // 2^(1/8): 1.0905
        if (target <= hi + (hi >> 4) + (hi >> 5) && target + (target >> 4) + (target >> 5) >= lo &&
            (previous == key_octave || (hi >> 1) >= (64u << 8)))
            return previous;
    }
    u32 code = key_octave, edge = bound;
    while (target <= (edge >> 1) && (edge >> 2) >= (64u << 8)) { edge >>= 1; ++code; }
    return code;
}
inline u32 octave_period(u32 root, u32 code) { return code >= key_octave && code - key_octave < 24 ? root >> (code - key_octave) : root; }
// A repeat time in samples (under 2^21), in 16ths of a tick: samples * bpm_x10 * 16
// / 275625, taken as (samples / 8) * bpm_x10 / 2153 to stay within 32 bits.
inline u32 samples_to_ticks16(u32 samples, u32 bpm_x10) { return quotient((samples >> 3) * bpm_x10, 2153); }
// The same without the divide, for comparing: samples_to_ticks16 is
// ticks16_product / 2153 rounded down, so it is at most a bound exactly when the
// product is under (bound + 1) * 2153 (bounds here are under 2^15).
inline u32 ticks16_product(u32 samples, u32 bpm_x10) { return (samples >> 3) * bpm_x10; }
inline bool ticks16_at_most(u32 product, u32 bound) { return product < (bound + 1) * 2153u; }
inline bool ticks16_below(u32 product, u32 bound) { return product < bound * 2153u; }
// nearest_division(samples_to_ticks16(samples, bpm_x10), previous), from the product.
inline u32 nearest_division_product(u32 product, u32 previous) {
    u32 k = 0;
    while (k + 1 < tempo_divisions) {
        u32 bound = division_bounds[k];
        if (previous && previous - 1 == k) bound += bound / 16;      // just above it
        else if (previous && previous - 1 == k + 1) bound -= bound / 16;  // just below it
        if (ticks16_at_most(product, bound)) break;
        ++k;
    }
    return k;
}
// The gate's release once the key is up: one smooth ramp from full level to silence,
// whose length the gate word sets. fade_step is how far the ramp moves each sample
// (Q31, a whole ramp being full scale): 0 releases nothing, so a hit rings out; it
// grows with gate cubed, to a 10 ms ramp at the top (80 ms at half, 640 ms at a
// quarter), so the whole knob is usable.
const i32 fastest_fade = 0x004a4dc9;  // Q31 1/441: 10 ms at 44.1 kHz
inline i32 fade_step(i32 gate) {
    if (gate <= 0) return 0;
    return q31::multiply(q31::multiply(q31::multiply(gate, gate), gate), fastest_fade);
}
// The level at a point of the ramp (Q31, 0 at key-up to full at its end): the
// smoothstep 1 - 3p^2 + 2p^3, written (1-p)^2 (1+2p). It leaves full level and
// reaches exactly 0 with no slope at either end, so neither the start of a short
// release nor its end steps; a hit whose ramp is through has ended.
const i32 released = 0x7fffffff;
inline i32 release_level(i32 p) {
    if (p <= 0) return released;
    if (p >= released) return 0;
    const i32 rest = q31::multiply(released - p, released - p);
    return rest + 2 * q31::multiply(p, rest);
}
// The quiet hold. A loud hit stopped quickly leaves the OP-XY a pop: its output
// switches off about 0.2 s after the sound falls silent and drops what the hit's low
// end left there (stock drumline's gate pops the same way). So from the first block
// of its release (key-up, or later if gate was 0 then) a gated note also plays a quiet
// tone at the Nyquist frequency (+level, -level, every other sample: 22 kHz,
// inaudible, no DC) and stays open until the tone ends, 1.25 s on, by when that is
// gone. The tone fades in and out over release_hold_fade blocks: switched on or off
// at once, the OP-XY's analog output clicks.
// The OP-XY's output switches off only when everything it plays is silent, so only
// the latest gated release needs to hold: one released before it fades its hold
// out at once (the latest one's hold outlasts it), and a stack of notes keeps one
// voice open, not all of them.
const i32 release_hold_blocks = 861;    // 1.25 s from the release's first block
const i32 release_hold_fade = 64;       // 93 ms (blocks; a power of two)
const i32 release_hold_level = 214748;  // -80 dB (Q31) at the voice

// One voice's sound, set at every start and render. The four controls are the
// model's own (stock's per-model param 1..4: the first is its pitch or colour,
// the second its length); random scales stock's per-hit variation. Controls and
// random are Q31 0..max; stock clamps each to the model's range.
struct Patch {
    u32 model;         // 0..models-1
    i32 random;
    i32 controls[4];
    bool keep_pitch;   // true: random leaves the first control (pitch) alone
    i32 tuning;        // Q30 factor on the played pitch (1 << 30 leaves it), applied by tuned()
};
// A pitch increment scaled by a Q30 factor, saturating: how render applies
// Patch::tuning to each sample's increment.
inline u32 tuned(u32 increment, i32 factor_q30) {
    const i32 half = q31::multiply(static_cast<i32>(increment), factor_q30);
    return half > 0x3fffffff ? 0x7fffffffu : static_cast<u32>(half) << 1;
}

// GUI only: the name stock drumline gives a model's control (stock models
// 0..models-1 in stock order, controls 0..3), or null; and the selected track's
// word at index (0..7 its controls) as its pages show it, a held step's lock
// included, for a machine's parameter_title(index): a
// drumline-voice synth defines `const char* parameter_title(opxy::u32 index)`,
// returning the title a parameter shows on the engine page and the LFO's
// destinations, or null for its declared label.
const char* control_name(u32 model, u32 control);
i32 selected_word(u32 index);
// Prepares the voice; call once from initialize, whose voice storage the SDK
// guarantees zeroed (the device binding relies on that and doesn't clear it again). A
// voice that is not constructed, or whose construction stock did not confirm,
// renders silence.
void construct(Voice& voice);
// Starts a hit: stock's note start for this voice and patch.
void start(Voice& voice, const Patch& patch);
// The voice is given up (the machine's destroy): a note it still holds open in
// stock's slot ends there, so a note that lost its voice to another doesn't keep
// its slot. Call it from destroy.
void end(Voice& voice);
// The note's key went up: looping stops and the gate fades the hit out. Call it
// from the machine's release (resources.releaseCallback).
void note_off(Voice& voice);
// One mono block into output (block_frames Q31 samples, at stock's drumline
// level). parameters and increments are the synth render's own: pitch follows
// the played note, scaled by the patch's tuning, with middle C (C4) at the
// model's own tuning when that is unity.
void render(Voice& voice, const Patch& patch, i32* output, const i32* parameters, const u32* increments);

}}  // namespace opxy::stock_drumline

#if defined(__bfin__)
extern "C" {
void sdk_drumline_construct(opxy::stock_drumline::Voice*) __asm__("sdk_drumline_construct");
void sdk_drumline_start(opxy::stock_drumline::Voice*, const opxy::stock_drumline::Patch*) __asm__("sdk_drumline_start");
void sdk_drumline_note_off(opxy::stock_drumline::Voice*) __asm__("sdk_drumline_note_off");
void sdk_drumline_end(opxy::stock_drumline::Voice*) __asm__("sdk_drumline_end");
const char* sdk_drumline_control_name(opxy::u32, opxy::u32) __asm__("sdk_drumline_control_name");
opxy::i32 sdk_drumline_selected_word(opxy::u32) __asm__("sdk_drumline_selected_word");
void sdk_drumline_render(opxy::stock_drumline::Voice*, const opxy::stock_drumline::Patch*, opxy::i32*, const opxy::i32*,
                         const opxy::u32*) __asm__("sdk_drumline_render");
}
namespace opxy { namespace stock_drumline {
inline void construct(Voice& voice) { sdk_drumline_construct(&voice); }
inline void start(Voice& voice, const Patch& patch) { sdk_drumline_start(&voice, &patch); }
inline void note_off(Voice& voice) { sdk_drumline_note_off(&voice); }
inline void end(Voice& voice) { sdk_drumline_end(&voice); }
inline const char* control_name(u32 model, u32 control) { return sdk_drumline_control_name(model, control); }
inline i32 selected_word(u32 index) { return sdk_drumline_selected_word(index); }
inline void render(Voice& voice, const Patch& patch, i32* output, const i32* parameters, const u32* increments) {
    sdk_drumline_render(&voice, &patch, output, parameters, increments);
}
}}
#else
// Host builds: no firmware, so silence.
namespace opxy { namespace stock_drumline {
inline void construct(Voice& voice) { for (u32 i = 0; i < 140; ++i) voice.words[i] = 0; }
inline void start(Voice&, const Patch&) {}
inline void note_off(Voice&) {}
inline void end(Voice&) {}
inline const char* control_name(u32, u32) { return nullptr; }
inline i32 selected_word(u32) { return 0; }
inline void render(Voice&, const Patch&, i32* output, const i32*, const u32*) {
    for (u32 i = 0; i < block_frames; ++i) output[i] = 0;
}
}}
#endif
