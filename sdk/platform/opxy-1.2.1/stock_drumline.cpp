// Stock OS 1.2.1 drumline voices run inside a synth machine's own voices
// (opxy/stock_drumline.hpp). Stock registers drumline as engine 4, a sample-flag
// engine whose voices its alternate renderer drives with a per-voice patch block
// built from each key's saved settings. Here the machine supplies that patch, the
// synth's normal render path supplies pitch, and stock's own drumline code
// renders. Linked only into collections with a machine declaring
// resources.stockVoice "drumline"; docs/abi-1.2.1.md, "Stock voices".
#include "opxy/stock_drumline.hpp"
#include "opxy/q31.hpp"
#include "stock.hpp"

using opxy::i32;
using opxy::u32;
using opxy::stock_drumline::Patch;
using opxy::stock_drumline::Voice;
using opxy::platform::stock;

namespace {
// Stock's drumline voice: the constructor its factory (0x8d513d1c) calls on
// each static voice, and the method table it installs.
const u32 kConstruct = 0x8d513a58;
const u32 kVtable = 0x8404ce3c;
// Method-table entries: +16 note start, +12 the alternate renderer's block call
// (stock's +8 asserts). +28, note release, is never called: at note-off the
// binding leaves loop mode, fades the hit out by the gate's release ramp
// (sdk_drumline_render) and keeps stock's amp envelope out of release (kAmpNoteOff).
const u32 kStart = 0x8d513bea, kRender = 0x11a06d28;
// Voice words the alternate renderer's note start (+20) sets before start: the
// key, and a Q31 offset its length control adds (0.5 adds nothing).
const u32 kKeyWord = 0x34 / 4, kLengthOffsetWord = 0x60 / 4;
// Note start (+16) draws a per-hit random value for each of the four controls
// at voice words 0x50..0x5c, which render scales by random and adds; the first
// is the pitch control's.
const u32 kPitchRandomWord = 0x50 / 4;
// The voice's attack ramp (+0x13c), which render's loop re-hit sets to exactly
// kRehitRamp after its note start (a first start sets 0x20c49c); render raises it
// each sample, so the value means a re-hit at the end of the block just rendered.
const u32 kRampWord = 0x13c / 4, kRehitRamp = 0x0147ae14;
static_assert(sizeof(Voice::words) == 560, "Stock 1.2.1 drumline voice object");

// The patch block word 0: stock's 66 models (family * 6 + variant) as the
// centre of each 1/66 of the Q31 range; render reads (word >> 8) * 66 >> 23.
const u32 kModelStep = 0x01f07c1f, kModelCentre = 0x00f83e0f;
const i32 kCentre = 0x40000000;          // pan centre; track offsets 0.5 add nothing
const i32 kUnityGain = 0x08000000;       // stock's 0 dB per-key gain word
const u32 kKey = 60;                      // the key stock records for the voice
const u32 kTrackWords = 16;               // render reads track words 0..11
// The track's four envelope words as this voice plays them, M2's knobs 1 to 4:
// loop, attack, gate and length; and drumline's own words for attack and length.
const u32 kLoop = 8, kAttack = 9, kGate = 10, kLength = 11;
const u32 kDrumlineAttack = 9, kDrumlineLength = 11;
// Stock's play modes per model: render re-hits a voice in loop mode once it has
// fallen to about -30 dB, and 0 is gate, whose fade only +28 starts.
const u32 kPlainMode = 0, kLoopMode = 3;
// Voice word render zeroes when a hit has died away; note start sets it again.
const u32 kSoundingWord = 0x4c / 4;

// The synth renderer's voice slot: its handle at +0x94, and its amp envelope's
// level at +0x18 and state at +0x24 (3 sustain, 4 release). In release, a level
// under about -66 dB moves it to 0, and stock stops rendering the note. The
// renderer keeps its slot at +8 in its frame.
const u32 kSlotHandle = 0x94, kAmpLevel = 0x18, kAmpState = 0x24, kFrameSlot = 8;
const i32 kAmpAttack = 1, kAmpSustain = 3, kAmpRelease = 4, kAmpTaken = 6;
const u32 kFullLevel = 0x7fffffff;
// Stock's note-off (0x8d51a928) sets this byte, the amp envelope's note-off flag,
// before it calls the voice's +28. The envelope's next update (0x8d50e110) then
// enters release at a rate read from track word 11, which this voice plays as
// its length: a long length is a short release, which can end the note within a
// block, before keep_note holds it. Cleared at note-off, the note plays on as if
// the key were held, and keep_note ends it once the hit has died.
const u32 kAmpNoteOff = 0x29;
// The voice's track word, and the slot byte stock's synth path sets it from (8d51aefc).
const u32 kTrackWord = 0x1b4 / 4, kSlotTrack = 0x55;
unsigned char* render_slot;
const void* render_handle;

// The note's life follows the hit rather than the words stock reads as its
// envelope: its amp envelope stays open while the hit sounds or the quiet hold
// plays, then ends at once.
// Only attack, decay and sustain (1..3) are this voice's to hold: when stock takes
// the slot for another note (8d51ad04) it sets state 6, a short fade, and starts
// the new note once that ends, so a fading or finished envelope is left alone.
void keep_note(bool sounding) {
    if (!render_slot) return;
    i32* state = reinterpret_cast<i32*>(render_slot + kAmpState);
    if (*state < kAmpAttack || *state > kAmpSustain) return;
    *state = sounding ? kAmpSustain : kAmpRelease;
    *reinterpret_cast<u32*>(render_slot + kAmpLevel) = sounding ? kFullLevel : 0;
}

// Drumline reads the per-sample pitch buffer as stock's alternate renderer fills
// it: phase increment >> 16, so that 0x800 is the model's own tuning. The synth
// path holds full increments; C4 (25480119 at 44.1 kHz) maps to 0x800, as the
// Q31 product increment * (0x800 << 31 / 25480119).
const u32 kPitch = 0x11902c00;
const i32 kC4Scale = 172607;

// Level, as stock plays drumline. Render scales each sample by the per-voice
// gain the alternate renderer fills at 0x11903400 and shifts it left by 7,
// saturating. The synth path applies volume and velocity after the voice, so the
// binding renders at full gain, and the synth path scales the voice by its
// engine's level method +40 (a synth's default gives about 1/32), so the
// machine's +40 returns drumline's own level. Measured on the device, a kick so
// rendered peaks within 0.1 dB of stock drumline's at the same track volume,
// with no saturation at this or any lower gain.
const u32 kGain = 0x11903400;
const u32 kFullGain = 0x7fffffff;
const u32 kLevel = 0x8d513a08;

bool ready(const Voice* voice) { return voice->words[0] == kVtable; }

// The per-sample passes below are kept out of line (noinline): inlined into
// sdk_drumline_render, whose other work leaves few registers free, the compiler
// built them as branching loops that pass samples through pointer registers; on
// their own each is a zero-overhead hardware loop.

// Keeps the synth path's increments in `saved` and writes drumline's pitch for each
// to the pitch buffer: tuned by the patch, then scaled so that C4 is 0x800. The
// tuning saturates through a minimum, with no branch: where tuned() gives 0x7fffffff
// this gives 0x7ffffffe, and the scaling multiply truncates both to the same pitch
// (checked on the host against tuned() for every increment and model tuning).
__attribute__((noinline)) void tune_pitch(const u32* increments, u32* saved, u32* pitch, i32 tuning) {
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        const u32 increment = increments[i];
        saved[i] = increment;
        i32 half = opxy::q31::multiply(static_cast<i32>(increment), tuning);
        half = half < 0x3fffffff ? half : 0x3fffffff;
        pitch[i] = static_cast<u32>(opxy::q31::multiply(static_cast<i32>(static_cast<u32>(half) << 1), kC4Scale));
    }
}
__attribute__((noinline)) void restore_pitch(u32* pitch, const u32* saved) {
    for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = saved[i];
}

// A ramp's next point: `step` (above 0) on, clamped at the end without a branch
// (both are under 2^31, so the sum doesn't wrap).
inline i32 ramp_on(i32 p, i32 step) {
    const u32 next = static_cast<u32>(p) + static_cast<u32>(step), end = static_cast<u32>(opxy::stock_drumline::released);
    return static_cast<i32>(next < end ? next : end);
}
// release_level's smoothstep for p above 0, without its branches: for p above 0 they
// give what the formula gives (at the ramp's end rest, and so the level, is exactly 0).
inline i32 smooth_level(i32 p) {
    const i32 rest = opxy::q31::multiply(opxy::stock_drumline::released - p, opxy::stock_drumline::released - p);
    return rest + 2 * opxy::q31::multiply(p, rest);
}
// The gate's release over a block (release_level): its ramp from voice->release by
// `step` (0 or more) a sample.
__attribute__((noinline)) void release_block(Voice* voice, i32* output, i32 step) {
    i32 p = voice->release;
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        p = ramp_on(p, step);
        output[i] = opxy::q31::multiply(output[i], smooth_level(p));
    }
    voice->release = p;
}
// Whether the gate's release has run out.
inline bool released_out(const Voice* voice) { return voice->release == opxy::stock_drumline::released; }
// Whether the quiet hold is still to play: the gate released the hit (its ramp has
// begun) and the hold's blocks aren't through. A held key, or gate 0, never holds.
// add_hold adds this block of it to `output`: full level between a linear fade in
// over its first release_hold_fade blocks and out over its last. A hold that a later
// gated release has overtaken (release_order) fades out from now, from the level it
// has reached, so its tone doesn't step.
inline bool holding(const Voice* voice) {
    return voice->release > 0 && voice->hold < opxy::stock_drumline::release_hold_blocks;
}
// The count of gate releases begun, by every voice on every drumline-voice track: the
// OP-XY's output stays on while any of them sounds, so only the latest one holds.
u32 latest_release = 0;
__attribute__((noinline)) void add_hold(Voice* voice, i32* output) {
    if (voice->hold > 0 && voice->release_order != latest_release) {
        const i32 in = voice->hold < opxy::stock_drumline::release_hold_fade ? voice->hold : opxy::stock_drumline::release_hold_fade;
        const i32 out = opxy::stock_drumline::release_hold_blocks - in;
        if (voice->hold < out) voice->hold = out;
    }
    const i32 fade = opxy::stock_drumline::release_hold_fade * static_cast<i32>(opxy::block_frames);
    const i32 level = opxy::stock_drumline::release_hold_level;
    const i32 start = voice->hold * static_cast<i32>(opxy::block_frames);
    const i32 end = opxy::stock_drumline::release_hold_blocks * static_cast<i32>(opxy::block_frames);
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        const i32 into = start + static_cast<i32>(i), left = end - into;
        const i32 k = into < fade ? into : left < fade ? left : fade;
        const i32 sample = static_cast<i32>((static_cast<u32>(level) * static_cast<u32>(k)) / static_cast<u32>(fade));
        output[i] += i & 1 ? -sample : sample;
    }
    ++voice->hold;
}

// The eight-word patch block stock's render reads (docs/abi-1.2.1.md, "Stock voices"):
// the model and play mode, the four controls, pan, random and unity gain.
void block(const Patch& patch, u32 mode, i32 (&out)[8]) {
    const u32 model = patch.model < opxy::stock_drumline::models ? patch.model : 0;
    out[0] = static_cast<i32>((model * 6 + mode) * kModelStep + kModelCentre);
    for (u32 i = 0; i < 4; ++i) out[1 + i] = patch.controls[i] < 0 ? 0 : patch.controls[i];
    out[5] = kCentre;
    out[6] = patch.random < 0 ? 0 : patch.random;
    out[7] = kUnityGain;
}

// Tempo loop (M2 knob 1's third setting). Stock re-hits a looping voice (render
// 11a07482..11a075fc, mode 3) once both its level words, +0x08 and +0x40, are below
// about -30 dB (0x8404d524); for the shaker (family 3) +0x0c must also be full.
// Render takes the family and mode from the model word: (word >> 8) * 66 >> 23,
// divided by 6. The tempo loop plays mode 0, so stock never re-hits, and restarts
// the hit itself at the tempo division nearest that natural repeat time, timed
// from the last hit. Until the hit reaches -30 dB the time is the last one
// measured for the same sound, else estimated from its fall since its peak.
const u32 kLevelWordA = 0x08 / 4, kLevelWordB = 0x40 / 4, kShakerWord = 0x0c / 4, kShaker = 3;
const i32 kLoopLevel = 0x040b7803;
// Stock's re-hit: +0x30 counts blocks since the hit, the ramp word marks a re-hit
// (kRehitRamp), and a re-hit of the selected track's voice posts slot 30 with
// count << 16 | track << 8 | key, its count from 0x8404ce0c.
const u32 kHitBlocksWord = 0x30 / 4, kRehitKeyWord = 0x34 / 4;
const u32 kRehitCount = 0x8404ce0c, kSelectedVoiceTrack = 0x8d4d6644, kPost = 0x8d4d666a, kLoopHitSlot = 30;

// Repeat times measured for recent sounds, so a new note of a sound already heard
// repeats at its time from the first hit. Audio renders one voice at a time.
struct Measured { u32 sound, samples; };
Measured measured_sounds[8];
u32 measured_next;
u32 recall(u32 sound) {
    for (u32 i = 0; i < 8; ++i)
        if (measured_sounds[i].samples && measured_sounds[i].sound == sound) return measured_sounds[i].samples;
    return 0;
}
void remember(u32 sound, u32 samples) {
    for (u32 i = 0; i < 8; ++i)
        if (measured_sounds[i].sound == sound) { measured_sounds[i].samples = samples; return; }
    measured_sounds[measured_next] = Measured{sound, samples};
    measured_next = (measured_next + 1) & 7;
}

// log2 of a positive level, Q8: the top bit's place, and the mantissa m (0..1)
// bent toward the curve by m + 0.346 m (1 - m), within 0.01 (a straight mantissa
// is out by up to 0.09, which put a -30 dB crossing several percent early).
i32 log2_q8(i32 level) {
    if (level <= 0) return 0;
    const u32 x = static_cast<u32>(level);
    // A positive level's top bit: 30 less its redundant sign bits, one instruction
    // on Blackfin rather than a search whose length depends on the level.
#if defined(__bfin__)
    i32 sign_bits;
    __asm__("%0.L = SIGNBITS %1; %0 = %0.L (Z);" : "=d"(sign_bits) : "d"(level));
    const i32 n = 30 - sign_bits;
#else
    i32 n = 30;
    while (!(x >> n)) --n;
#endif
    const u32 m = (x << (31 - n)) >> 15 & 0xffff;  // Q16
    const u32 bend = ((m * (0x10000 - m)) >> 16) * 89 >> 16;  // 0.346 m (1 - m), Q8 (89 / 256 = 0.348)
    return n * 256 + static_cast<i32>((m >> 8) + bend);
}
// log2_q8(kLoopLevel), the loop's level, worked out once (tests/fixtures check it).
const i32 kFloorLevel = 6658;

// What sync works out from the tempo alone, kept until the tempo changes, so a block
// divides only when it does: each division's period (division_period) and key mode's
// bound (key_bound), each worked out when first asked for. Audio renders one voice at
// a time and every voice plays at the project's one tempo.
struct TempoCache { u32 bpm_x10, valid, bound, period[opxy::stock_drumline::tempo_divisions]; };
TempoCache tempo_cache;
const u32 kBoundValid = 1u << 31;
void tempo_for(u32 bpm_x10) {
    if (tempo_cache.bpm_x10 != bpm_x10) {
        tempo_cache.bpm_x10 = bpm_x10;
        tempo_cache.valid = 0;
    }
}
u32 period_of(u32 index, u32 bpm_x10) {
    tempo_for(bpm_x10);
    if (!(tempo_cache.valid >> index & 1)) {
        tempo_cache.period[index] = opxy::stock_drumline::division_period(index, bpm_x10);
        tempo_cache.valid |= 1u << index;
    }
    return tempo_cache.period[index];
}
u32 bound_of(u32 bpm_x10) {
    tempo_for(bpm_x10);
    if (!(tempo_cache.valid & kBoundValid)) {
        tempo_cache.bound = opxy::stock_drumline::key_bound(bpm_x10);
        tempo_cache.valid |= kBoundValid;
    }
    return tempo_cache.bound;
}
// The played key's own period (key_period, a long division), kept for recent pitch
// steps in a table every voice shares, so a note starting on a key heard before
// (any voice, any track) reuses it: a pattern's notes starting together don't
// each divide. key_period is never 0, so 0 marks an empty entry.
struct KeyPeriod { u32 increment, period; };
KeyPeriod key_periods[64];
u32 period_for(u32 increment) {
    KeyPeriod& entry = key_periods[(increment ^ increment >> 6 ^ increment >> 12 ^ increment >> 18) & 63];
    if (entry.increment != increment || !entry.period) {
        entry.increment = increment;
        entry.period = opxy::stock_drumline::key_period(increment);
    }
    return entry.period;
}

// Voice::natural while the loop isn't synced: the next synced block restarts.
// A marker, not a time: natural otherwise holds the hit's measured fall (samples,
// under 2^21), so it can't be mistaken for one.
const u32 kEntering = 0xffffffffu;
void restart_tempo(Voice* voice) {
    voice->clock = voice->struck = 0;
    voice->note = 0;
    voice->peak_level = voice->last_level = 0;
    voice->natural = 0;
    voice->delay = voice->prior_delay = voice->fresh = 0;
}

// After a block of a synced hit: where in the next block its restart lands
// (samples, Q8), or -1 for none yet.
//
// Its repeat time is the one it falls to -30 dB at, measured on stock's clock (the
// hit restarts there at a block's start and is heard `delay` later). The first hit
// of a key plays until that is measured; later ones restart before it if their
// division is shorter, and then carry the time on from their fall (`exact` stays
// for a measured time while the sound is unchanged; a changed sound keeps the
// last time and corrects it, rather than stopping to measure).
//
// It repeats every tempo division nearest that time or, shorter than 1/64, as a
// tone: the played key's pitch (`increment`: its phase step a sample, 2^32 a
// cycle) in its longest octave under 1/64, an octave up for each octave shorter
// that time is, no shorter than a block.
// Each repeat is timed from the last, as the free loop's are, and never earlier
// than the end of the block just rendered; every gap between divisions is a whole
// division.
i32 tempo_rehit(Voice* voice, u32 bpm_x10, u32 sound, u32 family, u32 increment) {
    if (sound != voice->sound) {
        voice->sound = sound;
        voice->exact = 0;
        if (const u32 known = recall(sound)) {
            voice->measured = known;
            voice->exact = 1;
        }
    }
    voice->clock += static_cast<u32>(opxy::block_frames) << 8;
    const u32 now = voice->clock;  // the next block's start
    // Just synced, the hit playing started under the free loop or one-shot, when
    // this clock wasn't running, so its start is unknown: restart it at the next
    // block, as a note does, and measure from there.
    if (voice->natural == kEntering) {
        voice->struck = now;
        voice->peak_level = voice->last_level = 0;
        voice->peak_at = 0;
        voice->natural = 0;
        return 0;
    }
    if (static_cast<i32>(now - voice->struck) <= 0) return -1;  // times wrap: compare differences
    const u32 heard = (now - voice->struck) >> 8;  // samples of the hit heard by then
    const u32 played = heard + (voice->delay >> 8);  // on stock's clock: whole blocks
    const i32 a = static_cast<i32>(voice->words[kLevelWordA]), b = static_cast<i32>(voice->words[kLevelWordB]);
    const i32 level = a > b ? a : b;
    const i32 lg = log2_q8(level), floor_level = kFloorLevel, before = voice->last_level;
    if (lg > voice->peak_level) {
        voice->peak_level = lg;
        voice->peak_at = played;
    }
    // Its fall since its peak carried on to the loop's level, once it has fallen a
    // quarter of an octave: the time it should reach that level (samples), or 0.
    // Measured from the peak, so a slow fall still moves the level by whole steps
    // (32-bit: Q8 levels are under 8192, blocks since the peak held under 2^15).
    u32 projected = 0;
    if (voice->peak_level - lg >= 64 && lg > floor_level && played > voice->peak_at) {
        u32 blocks = (played - voice->peak_at) >> 6;
        if (blocks > 32767) blocks = 32767;
        // left * blocks / fallen blocks, kept to the sample: whole blocks, then the
        // remainder's share of one.
        const u32 n = static_cast<u32>(lg - floor_level) * (blocks ? blocks : 1), d = static_cast<u32>(voice->peak_level - lg);
        const u32 whole = opxy::stock_drumline::quotient(n, d);
        projected = played + (whole << 6) + opxy::stock_drumline::quotient((n - whole * d) << 6, d);
    }
    voice->last_level = lg;
    // Fallen to the loop's level once it has risen above it: a hit still rising
    // from silence at its start has not yet sounded.
    // A hit never seen above it that is already falling (or silent) a block on has
    // died within its first block: its time is a block, the shortest restart.
    const bool within_block = voice->peak_level <= floor_level && played >= 128 && (lg < before || level <= 0);
    if (!voice->natural && level < kLoopLevel && (voice->peak_level > floor_level || within_block) &&
        (family != kShaker || voice->words[kShakerWord] == 0x7fffffffu)) {
        // It crossed during this block: where, from its level at either end (log2,
        // so a steady fall is a straight line). Within 16 samples of the time last
        // measured for this sound it is the same time: keeping that stops a hit
        // near a bound alternating between two repeats.
        u32 into = 32;  // samples into the block, if its level then is unknown
        if (before > floor_level && before > lg)
            into = opxy::stock_drumline::quotient(static_cast<u32>(before - floor_level) * 64, static_cast<u32>(before - lg));
        if (into > 64) into = 64;
        const u32 at = within_block ? 64 : (played > 64 ? played - 64 : 0) + into, last = voice->measured;
        voice->natural = at ? at : 1;
        if (!(voice->exact && voice->natural + 16 >= last && voice->natural <= last + 16)) voice->measured = voice->natural;
        voice->exact = 1;
        remember(sound, voice->measured);
    }
    u32 expected = voice->measured;
    // A sound not yet heard: until it reaches -30 dB, the time its fall projects.
    if (!expected) expected = projected;
    if (!expected) return -1;
    if (expected > (1u << 21) - 1) expected = (1u << 21) - 1;  // about 47 seconds
    // The time in 16ths of a tick, before its division (ticks16_product): compared
    // against bounds scaled to match, with no divide.
    const u32 product = opxy::stock_drumline::ticks16_product(expected, bpm_x10);
    u32 length;  // the repeat's length, samples, Q8
    bool note = false;
    u32 nearest = 0;
    if (opxy::stock_drumline::ticks16_below(product, opxy::stock_drumline::note_sync_bound)) {
        // Short of 1/64 the repeats sound as a tone: the played key's own pitch
        // (`increment`: transpose, detune and every other pitch change included)
        // in its longest octave under 1/64, an octave up for each octave shorter
        // the knob's time is. The step follows the time alone, the same for every key.
        const u32 bound = bound_of(bpm_x10);
        voice->note = opxy::stock_drumline::key_step(expected < (1u << 16) ? expected << 8 : (1u << 24) - 1, bound, voice->note);
        length = opxy::stock_drumline::octave_period(opxy::stock_drumline::key_root(period_for(increment), bound), voice->note);
        note = true;
    } else {
        voice->note = 0;
        nearest = opxy::stock_drumline::nearest_division_product(product, voice->division);
        length = period_of(nearest, bpm_x10);
    }
    if (!length) return -1;
    // Timed from the last hit, as the free loop's repeats are, so turning the knob
    // changes the next gap and nothing else. Should that time have passed (the
    // length just shortened), a division's gap stays a real length: the next
    // division up that hasn't passed; a tone restarts at once, as the free loop
    // does, one short cycle being less heard than a missing one.
    u32 due = voice->struck + length;
    if (static_cast<i32>(due - now) < 0) {
        const u32 elapsed = now - voice->struck;
        if (note) {
            due = now;
        } else {
            u32 k = nearest;
            while (k + 1 < opxy::stock_drumline::tempo_divisions && period_of(k, bpm_x10) < elapsed) ++k;
            length = period_of(k, bpm_x10);
            due = voice->struck + (length < elapsed ? opxy::stock_drumline::quotient(elapsed + length - 1, length) * length : length);
        }
    }
    if (due - now >= static_cast<u32>(opxy::block_frames) << 8) return -1;
    // Restarting before it fell to -30 dB: carry its time on from its fall in the
    // last block, unless measured for this sound.
    if (!voice->natural && !voice->exact && projected) voice->measured = projected < (1u << 21) ? projected : (1u << 21) - 1;
    voice->struck = due;
    voice->peak_level = voice->last_level = 0;
    voice->peak_at = 0;
    voice->natural = 0;
    if (!note) voice->division = nearest + 1;
    return static_cast<i32>(due - now);  // Q8
}

// The old hit, restarted at the end of this block, fades to silence across it, as
// stock's loop does: from full at its first sample to nothing at its last.
void fade_tail(i32* output) {
    const i32 frames = static_cast<i32>(opxy::block_frames), step = 0x7fffffff / (frames - 1);
    i32 gain = step * (frames - 1);
    for (i32 i = 0; i < frames; ++i, gain -= step) output[i] = opxy::q31::multiply(output[i], gain);
}

// Linear interpolation between neighbouring samples x and y, `fraction` (0..255)
// of the way from y back to x, as the delay has always worked it out.
inline i32 lerp(i32 x, i32 y, i32 fraction) { return x + ((y >> 8) - (x >> 8)) * fraction; }

// Delays the block by the hit's own offset (`delay`, samples, Q8), fractions by
// linear interpolation, so a restart sounds on its exact sample. The block before
// (`pending`) supplies what comes before. Just after a restart (`fresh`) that is
// the old hit, `prior_delay` late, whose faded tail plays on over the new hit's
// start rather than being cut short.
//
// Sample i of a stream `delay` late reads the stream at i - delay: whole samples
// back, plus a fraction. Each part of the block reads only the side it can reach:
// the block before (the new hit's silence across a restart) up to the delay, this
// block after it. The old hit's tail reads only the block before, so it reaches
// just the first samples of this one; past them it adds nothing.
// Out of line, as the passes above, so its loops are hardware loops; the block
// copies go through copy_block, which compiles to one (here it didn't).
__attribute__((noinline)) void copy_block(i32* to, const i32* from) {
    for (u32 i = 0; i < opxy::block_frames; ++i) to[i] = from[i];
}
__attribute__((noinline)) void delay_block(Voice* voice, i32* output) {
    const i32 d = static_cast<i32>(voice->delay), dp = static_cast<i32>(voice->prior_delay);
    const bool fresh = voice->fresh;
    voice->fresh = 0;
    if (!d && !dp) return;
    const i32 frames = static_cast<i32>(opxy::block_frames);
    static i32 now[opxy::block_frames];  // this block as rendered; one voice renders at a time
    i32* const before = voice->pending;
    copy_block(now, output);
    // The new (or only) hit.
    {
        const i32 whole = d >> 8, part = d & 0xff;
        i32 i = 0;
        if (!part) {
            if (fresh) for (; i < whole; ++i) output[i] = 0;
            else for (; i < whole; ++i) output[i] = before[frames - whole + i];
            for (; i < frames; ++i) output[i] = now[i - whole];
        } else {
            // x at i - s, y the sample after it (s is at most a block).
            const i32 fraction = 256 - part, s = whole + 1;
            if (fresh) for (; i < s - 1; ++i) output[i] = 0;
            else for (; i < s - 1; ++i) output[i] = lerp(before[frames - s + i], before[frames - s + i + 1], fraction);
            output[i] = lerp(fresh ? 0 : before[frames - 1], now[0], fraction);
            for (++i; i < frames; ++i) output[i] = lerp(now[i - s], now[i - s + 1], fraction);
        }
    }
    // The old hit's tail, across a restart (at no delay it reads only this block,
    // where it is silent).
    if (fresh && dp) {
        const i32 whole = dp >> 8, part = dp & 0xff;
        if (!part) {
            for (i32 i = 0; i < whole; ++i) output[i] += before[frames - whole + i];
        } else {
            const i32 fraction = 256 - part, s = whole + 1;
            for (i32 i = 0; i < s - 1; ++i) output[i] += lerp(before[frames - s + i], before[frames - s + i + 1], fraction);
            output[s - 1] += lerp(before[frames - 1], 0, fraction);
        }
    }
    copy_block(before, now);
}

// Restarts the hit as stock's loop does: note start, then the re-hit's own marks
// and, for the selected track's voice, the post the M1 animation follows.
void rehit(Voice* voice) {
    stock<void(Voice*)>(kStart)(voice);
    voice->words[kHitBlocksWord] = 1;
    voice->words[kRampWord] = kRehitRamp;
    if (stock<u32()>(kSelectedVoiceTrack)() != voice->words[kTrackWord]) return;
    volatile u32* count = reinterpret_cast<volatile u32*>(kRehitCount);
    const u32 n = *count + 1;
    *count = n;
    stock<void(u32, u32)>(kPost)(kLoopHitSlot, n << 16 | voice->words[kTrackWord] << 8 | (voice->words[kRehitKeyWord] & 0xff));
}
}  // namespace

extern "C" {
// Called by the adapter just before a voice renders, with the handle stock is
// rendering and the adapter's own frame, whose first word links to stock's. A slot
// that doesn't name the handle isn't kept: stock's own envelope then ends the note.
void sdk_drumline_caller(const void* handle, const void* frame) __asm__("sdk_drumline_caller");
void sdk_drumline_caller(const void* handle, const void* frame) {
    render_slot = nullptr;
    render_handle = handle;
    if (!frame) return;
    const unsigned char* stock_frame = *static_cast<const unsigned char* const*>(frame);
    if (!stock_frame) return;
    const u32 slot = *reinterpret_cast<const u32*>(stock_frame + kFrameSlot);
    // Stock allocates its voice slots at run time, so only rule out what can't be
    // one: a slot is word aligned, in external RAM (0x80000000..0x8fffffff) or
    // on-chip memory (0x08000000..0x0fffffff, 0x11800000..0x11bfffff). The handle
    // check below is what identifies the voice's own slot.
    if (slot & 3) return;
    if (!(slot >> 28 == 8 || slot >> 27 == 1 || (slot >= 0x11800000 && slot < 0x11c00000))) return;
    unsigned char* candidate = reinterpret_cast<unsigned char*>(slot);
    if (*reinterpret_cast<const void* const*>(candidate + kSlotHandle) == handle) render_slot = candidate;
}

// The stock-voice synth's method +40 (voice.cpp.in names it).
i32 sdk_drumline_level(const void*) __asm__("sdk_drumline_level");
i32 sdk_drumline_level(const void*) {
    return stock<i32(const void*)>(kLevel)(nullptr);
}

// The machine's initialize calls this on storage the adapter has already zeroed
// (voice.cpp.in), so stock's object is not cleared again here; the binding's own
// fields are set explicitly.
void sdk_drumline_construct(Voice* voice) {
    voice->held = 0;
    voice->slot = nullptr;
    voice->handle = nullptr;
    voice->release = 0;
    voice->hold = 0;
    voice->release_order = 0;
    restart_tempo(voice);
    stock<void(Voice*)>(kConstruct)(voice);
}

void sdk_drumline_start(Voice* voice, const Patch* patch) {
    if (!ready(voice)) return;
    voice->held = 1;
    voice->release = 0;
    voice->hold = 0;
    voice->release_order = 0;
    restart_tempo(voice);
    voice->words[kKeyWord] = kKey;
    voice->words[kLengthOffsetWord] = kCentre;
    stock<void(Voice*)>(kStart)(voice);
    (void)patch;
}

void sdk_drumline_note_off(Voice* voice) {
    voice->held = 0;
    // The slot this voice last rendered in, if it still plays this voice's handle.
    unsigned char* slot = static_cast<unsigned char*>(voice->slot);
    if (slot && *reinterpret_cast<const void* const*>(slot + kSlotHandle) == voice->handle) slot[kAmpNoteOff] = 0;
}

// The voice is given up: the slot it last rendered in, while it still plays this
// voice's handle and keep_note holds it in sustain, goes to release at level 0, so
// stock ends the note. A slot stock is fading or has restarted for another note is
// in another state and stays as it is.
void sdk_drumline_end(Voice* voice) {
    unsigned char* slot = static_cast<unsigned char*>(voice->slot);
    if (!slot || *reinterpret_cast<const void* const*>(slot + kSlotHandle) != voice->handle) return;
    i32* state = reinterpret_cast<i32*>(slot + kAmpState);
    if (*state != kAmpSustain) return;
    *state = kAmpRelease;
    *reinterpret_cast<u32*>(slot + kAmpLevel) = 0;
}

void sdk_drumline_render(Voice* voice, const Patch* patch, i32* output, const i32* parameters,
                         const u32* increments) {
    if (!ready(voice)) {
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    voice->slot = render_slot;
    voice->handle = render_handle;
    // The voice's track, as stock's synth path sets it before render (8d51aefc, from
    // slot byte 0x55): render posts a loop re-hit to the GUI only for the selected
    // track's voice, and the screen re-triggers on it (screen.hpp loop_hit).
    if (render_slot) voice->words[kTrackWord] = render_slot[kSlotTrack];
    const bool loop = voice->held && opxy::stock_drumline::looping(parameters[kLoop]);
    // A hit that has died away or faded out, with nothing to start another: stock has
    // nothing to add.
    if (!loop && (voice->words[kSoundingWord] == 0 || released_out(voice))) {
        // A hit the gate released plays out the quiet hold (release_hold_blocks) before
        // it falls silent, at full gain from the renderer unless stock is taking the
        // slot, whose fade stays; the note stays open meanwhile.
        if (holding(voice)) {
            for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
            add_hold(voice, output);
            if (!(render_slot && *reinterpret_cast<const i32*>(render_slot + kAmpState) >= kAmpTaken)) {
                u32* gain = reinterpret_cast<u32*>(kGain);
                for (u32 i = 0; i < opxy::block_frames; ++i) gain[i] = kFullGain;
            }
            keep_note(true);
            return;
        }
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        keep_note(false);
        return;
    }
    // The tempo loop restarts the hit itself (tempo_rehit), so stock plays it once;
    // with no tempo to follow it loops as stock does.
    const u32 bpm_x10 = loop && opxy::stock_drumline::tempo_synced(parameters[kLoop]) ? opxy::platform::audio_bpm_x10() : 0;
    i32 words[8];
    block(*patch, loop && !bpm_x10 ? kLoopMode : kPlainMode, words);
    if (patch->keep_pitch) voice->words[kPitchRandomWord] = 0;
    // Track words 0..7 offset the controls about 0.5: neutral, so the machine's
    // patch sets them. Words 8..11 are a synth's amp envelope; on a drumline track
    // they are its funk, attack, random and length, which render reads as its
    // attack (9), random added squared (10) and a length offset about 0.5 (11).
    // This voice plays them as loop, attack, gate and length: attack and length are
    // drumline's own, with its ranges, gate fades the hit once the key is up (below),
    // and funk and the added random stay off.
    // Scratch written and read every block lives on the stack, in fast memory, not in
    // statics, which sit in external memory: the track words and the saved pitch
    // (320 bytes). stock_gain is used only in a block stock is taking the voice, so it
    // is a static, copied only then (below).
    i32 track[kTrackWords];
    u32 saved[opxy::block_frames];
    static u32 stock_gain[opxy::block_frames];
    for (u32 i = 0; i < 8; ++i) track[i] = kCentre;
    for (u32 i = 8; i < kTrackWords; ++i) track[i] = 0;
    track[kDrumlineAttack] = parameters[kAttack];
    track[kDrumlineLength] = parameters[kLength];
    u32* pitch = reinterpret_cast<u32*>(kPitch);
    u32* gain = reinterpret_cast<u32*>(kGain);
    // Stock filled the gain buffer from its amp envelope, which reads these words
    // as attack, decay, sustain and release, and applies it after the voice. So the
    // binding leaves full gain there: the voice's own decay shapes the note. The
    // buffer is refilled for each voice. The pitch buffer holds the synth path's
    // increments (`increments` is the same buffer): kept for after the voice, then
    // each tuned by the patch and scaled to drumline's form. Plain loops, one job
    // each, so each compiles to a few instructions a sample.
    // Stock takes the slot for another note (8d51ad04, state 6) before the block
    // renders, and the voice doesn't change the state, so whether the gain stock
    // filled will be needed again (below) is known now.
    const bool taken = render_slot && *reinterpret_cast<const i32*>(render_slot + kAmpState) >= kAmpTaken;
    if (taken)
        for (u32 i = 0; i < opxy::block_frames; ++i) stock_gain[i] = gain[i];
    for (u32 i = 0; i < opxy::block_frames; ++i) gain[i] = kFullGain;
    tune_pitch(increments, saved, pitch, patch->tuning);
    stock<void(Voice*, i32*, const i32*, const i32*)>(kRender)(voice, output, words, track);
    restore_pitch(pitch, saved);
    // The sound's repeat time follows its model, controls, attack and length.
    const u32 sound = words[0] ^ static_cast<u32>(words[3]) * 3 ^ static_cast<u32>(words[4]) * 5 ^
                      static_cast<u32>(parameters[kAttack]) * 7 ^ static_cast<u32>(parameters[kLength]) * 11;
    const i32 offset = bpm_x10 ? tempo_rehit(voice, bpm_x10, sound, patch->model, saved[opxy::block_frames - 1]) : -1;
    // Out of sync the hit's start is unknown to the synced clock: the next synced
    // block restarts it (kEntering).
    if (!bpm_x10) voice->natural = kEntering;
    if (offset >= 0) rehit(voice);
    // A looping hit that fell to about -30 dB was restarted at the end of this block,
    // after its samples (stock's loop), or is restarted for a synced loop `offset`
    // samples into the next: note start drops the old tail between this block and
    // the next and sets the attack ramp to exactly kRehitRamp. So the tail fades to
    // silence across this block, and the new hit rises from silence on its own
    // sample (delay_block), under the end of that fade, instead of the tail being cut.
    const u32 delay = voice->delay;
    const bool restarted = loop && voice->words[kRampWord] == kRehitRamp;
    if (restarted) fade_tail(output);
    delay_block(voice, output);
    voice->prior_delay = delay;
    if (restarted) {
        voice->delay = offset >= 0 ? static_cast<u32>(offset) : 0;
        voice->fresh = 1;
    }
    // When stock takes the slot for another note (8d51ad04, state 6) its envelope
    // fades over this block, as on any synth (docs/abi-1.2.1.md, Synth voices): the
    // gain stock filled goes back, so the renderer's multiply after the voice applies
    // that fade instead of cutting the note at the restart.
    if (taken)
        for (u32 i = 0; i < opxy::block_frames; ++i) gain[i] = stock_gain[i];
    // Once the key is up, gate releases the hit from wherever it is: a ramp from full
    // level to silence, as long as gate sets, under the quiet hold (add_hold), which
    // keeps the note open after it.
    // The length word isn't moved: stock reads it as a squared offset, so changing it
    // barely shortens a hit until the top of its range, and a jump in it can step.
    const i32 step = voice->held ? 0 : opxy::stock_drumline::fade_step(parameters[kGate]);
    if (step > 0 && voice->release == 0) voice->release_order = ++latest_release;
    // A release once begun is applied every block, whatever gate does next: gate turned
    // to 0 mid-release (or so low its step rounds to 0) holds the hit at the level
    // the ramp has reached instead of jumping back to full.
    if (step > 0 || voice->release > 0) release_block(voice, output, step);
    if (holding(voice)) add_hold(voice, output);
    keep_note(loop || (voice->words[kSoundingWord] != 0 && !released_out(voice)) || holding(voice));
}
}

// The name stock drumline gives model's control: its per-model record (116 bytes
// from 0x8404ceec, the model's name then, at +96, its four controls' names).
const char* sdk_drumline_control_name(u32 model, u32 control) __asm__("sdk_drumline_control_name");
const char* sdk_drumline_control_name(u32 model, u32 control) {
    if (model >= 11 || control >= 4) return nullptr;
    return *reinterpret_cast<const char* const*>(0x8404ceec + 116 * model + 96 + 4 * control);
}

// The selected track's word at index as its pages show it: stock's displayed
// value 8d5ac9dc(project, track, index, 1), which is a held step's lock, else the
// live value while playing (its locks as they play) or the base value, so names
// chosen from it follow a locked model as the model's own value does.
opxy::i32 sdk_drumline_selected_word(u32 index) __asm__("sdk_drumline_selected_word");
opxy::i32 sdk_drumline_selected_word(u32 index) {
    return stock<i32(u32, u32, u32, u32)>(0x8d5ac9dc)(0x86bd027c, opxy::platform::selected_track(), index, 1);
}
