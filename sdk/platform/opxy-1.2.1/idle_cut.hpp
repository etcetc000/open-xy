// Idle cut (idle_cut.cpp): the project's idle threshold (the release_floor
// setting, docs/abi-1.2.1.md "Release floor") for every kind of voice, not only
// the normal renderer's released synth notes. A voice is cut only once nothing can
// make it louder again:
//
// 1. Its note is not held. A held note is never cut at any level, so an LFO that
//    dips a drone always gets it back. A one-shot hit is released from its start.
//    An SDK kit's voices have no gate the adapter reads: there the engine's
//    `settled` answers for the note (rule 2), and a kit whose voice can be held
//    returns false while it is.
// 2. What is judged can only fall: an envelope's own level in release (stock's
//    floor, unchanged), or an engine that says no more energy is coming (a
//    machine's `settled` hook: nothing scheduled, controls latched), or for stock's
//    drum sampler the loudest the sample can still play from its position to its
//    end (a table of chunk maxima built once per sample).
// 3. The threshold is relative to the note's own peak, and a cut voice fades, never
//    a hard cut: over one block through stock's own kill (8d51a998), or over the
//    SDK kit adapter's 128-frame choke fade.
//
// This header is the arithmetic, free of stock memory, so the host tests run it as
// it is.
#pragma once
#include <opxy/q31.hpp>
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace idle_cut {

// Blocks an engine's settled output must stay below the threshold before it goes:
// 8 (12 ms) rides out the beating of a decaying metal or noise voice.
const u32 kHold = 8;

// A sample's magnitude, -2^31 counting as 2^31 - 1: on the Blackfin its
// saturating ABS, one instruction and no branch (it runs on every sample of
// every block of a voice whose engine settles).
inline i32 magnitude(i32 x) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = ABS %1;" : "=d"(result) : "d"(x) : "ASTAT");
    return result;
#else
    return x < 0 ? (x == -0x7fffffff - 1 ? 0x7fffffff : -x) : x;
#endif
}
// The largest magnitude in a block.
inline i32 peak(const i32* x, u32 n) {
    i32 most = 0;
    for (u32 i = 0; i < n; ++i) {
        const i32 v = magnitude(x[i]);
        most = v > most ? v : most;
    }
    return most;
}

// A settled voice's follower: its note's peak so far and how many blocks in a row
// it has stayed below `floor` (Q31) times that peak while its engine is settled.
struct Follower {
    i32 reference;
    u32 quiet;
};
inline void restart(Follower& f) {
    f.reference = 0;
    f.quiet = 0;
}
// One rendered block of the voice; true once it may go. `floor` 0 is off.
inline bool follow(Follower& f, i32 block_peak, bool settled, i32 floor) {
    if (block_peak > f.reference) f.reference = block_peak;
    const bool under = floor > 0 && f.reference > 0 && block_peak <= q31::multiply(f.reference, floor);
    f.quiet = settled && under ? f.quiet + 1 : 0;
    return f.quiet >= kHold;
}

// The release floor in force, found by asking stock's envelope routine
// (8d50e110) where a release ends: `stage(level)` runs it once on a released
// envelope at that level and gives the stage it leaves. The setting rewrites the
// floor and has a release below it enter stage 5 (a one-block fade); stock enters
// stage 0. So 0 (off) when a release at level 0 does not enter 5, else the largest
// level that does.
template <class Stage> i32 probe_floor(Stage stage) {
    if (stage(0) != 5) return 0;
    i32 low = 0, high = 0x7fffffff;  // stage(low) is 5; stage(high) taken as not
    while (high - low > 1) {
        const i32 middle = low + (high - low) / 2;
        if (stage(middle) == 5) low = middle;
        else high = middle;
    }
    return low;
}

// --- Stock's drum sampler (engine 3) ---------------------------------------
//
// A sample's table: the largest magnitude of each chunk of frames (both channels),
// and from each chunk to the end. Built a few hundred samples a block, from the
// start; usable for a voice once the whole sample is scanned.
const u32 kChunks = 512;
const u32 kMinShift = 8;  // chunks of at least 256 frames (5.8 ms)
const u32 kSlots = 16;    // samples with a table at once
const u32 kScanBudget = 1024;  // samples scanned a block, all tables together

struct Table {
    const i16* pcm;
    u32 frames, stereo, signature;
    u32 shift, chunks;
    u32 scanned;        // chunks whose maximum is known, from the start
    u32 cursor;         // the next frame of the chunk being scanned
    i32 running;        // its maximum so far
    u32 used;           // when a voice last used it (a block count)
    bool complete;      // every chunk scanned and the suffix maxima worked out
    u16 chunk_max[kChunks];
    u16 suffix_max[kChunks];
};

inline u32 shift_for(u32 frames) {
    u32 s = kMinShift;
    while (s < 31 && ((frames - 1) >> s) >= kChunks) ++s;
    return s;
}
// last * k / 7 for k 0..7, without a 64-bit product: with last = 7q + r it is
// q * k + r * k / 7 exactly (q * k <= last and r * k <= 42, so neither overflows).
inline u32 spread(u32 last, u32 k) {
    const u32 q = last / 7, r = last % 7;
    return q * k + r * k / 7;
}
// A few samples spread over the sample, against a different sample put in the
// same place with the same length.
inline u32 signature(const i16* pcm, u32 frames, u32 stereo) {
    const u32 samples = frames << (stereo ? 1 : 0);
    u32 sum = frames * 2654435761u;
    for (u32 k = 0; k < 8; ++k) {
        const u32 at = spread(samples - 1, k);
        sum = (sum ^ static_cast<u16>(pcm[at])) * 16777619u;
    }
    return sum;
}
inline void begin(Table& t, const i16* pcm, u32 frames, u32 stereo, u32 now) {
    t.pcm = pcm;
    t.frames = frames;
    t.stereo = stereo ? 1 : 0;
    t.signature = signature(pcm, frames, t.stereo);
    t.shift = shift_for(frames);
    t.chunks = (frames + (1u << t.shift) - 1) >> t.shift;
    t.scanned = 0;
    t.cursor = 0;
    t.running = 0;
    t.used = now;
    t.complete = false;
}
inline bool matches(const Table& t, const i16* pcm, u32 frames, u32 stereo) {
    return t.pcm == pcm && t.frames == frames && t.stereo == (stereo ? 1u : 0u);
}
// Scans up to `budget` samples; returns what is left of the budget.
inline u32 scan(Table& t, u32 budget) {
    const u32 channels = t.stereo + 1;
    while (!t.complete && budget) {
        if (t.scanned == t.chunks) {
            u16 most = 0;
            for (u32 c = t.chunks; c-- > 0;) {
                if (t.chunk_max[c] > most) most = t.chunk_max[c];
                t.suffix_max[c] = most;
            }
            t.complete = true;
            break;
        }
        const u32 end = (t.scanned + 1) << t.shift;
        const u32 last = end < t.frames ? end : t.frames;
        u32 frames = last - t.cursor;
        if (frames * channels > budget) frames = budget / channels ? budget / channels : 1;
        const i16* p = t.pcm + (t.cursor << t.stereo);
        i32 most = t.running;
        for (u32 i = 0; i < frames * channels; ++i) {
            const i32 v = p[i] < 0 ? -p[i] : p[i];
            if (v > most) most = v;
        }
        t.running = most;
        t.cursor += frames;
        budget = budget > frames * channels ? budget - frames * channels : 0;
        if (t.cursor >= last) {
            t.chunk_max[t.scanned++] = static_cast<u16>(most > 32767 ? 32767 : most);
            t.running = 0;
        }
    }
    return budget;
}

// One drum sampler voice as the cut sees it, read from stock's objects each block.
struct Sampler {
    u32 state;      // 0 done, 1 playing (held in gate and loop modes), 2 released
    u32 mode;       // play mode: 0 gate, 3 loop, others one-shot
    bool reverse;
    u32 position;   // frames
    u32 start;      // frames: where the note's playback starts
    i32 level;      // the level the sample plays at now (+0x34: its curve x the pad's level)
    i32 pad_level;  // the pad's level, which the curve never exceeds
    u32 count;      // blocks since the note started (stock's +0x40)
};
// What the cut follows for one voice: the note's peak (reference), in level x
// sample units, from the chunks the note has played in full.
struct Track {
    u32 count;      // the stock count it last saw: a smaller one is a new note
    u32 start;      // the start and position it last saw (new_note)
    u32 position;
    u32 next;       // the next chunk whose maximum the reference has not taken
    i32 reference;
    bool started;
};
// Level x magnitude, as the voice's gain applies it (Q31 x Q15, scaled by 2^-15).
inline i32 scaled(i32 level, u32 magnitude) {
    return q31::multiply(level < 0 ? 0 : level, static_cast<i32>(magnitude << 16));
}
// A new note on the voice: stock's block count (+0x40) went back, which a note
// seen for a block or two may not show, or the note's start moved, or the
// position went back while the note cannot loop (stock loops only a held note
// in loop mode: 11a07c2c) and does not play in reverse.
inline bool new_note(const Track& k, const Sampler& s) {
    const bool loops = s.state == 1 && s.mode == 3;
    return !k.started || s.count < k.count || s.start != k.start ||
           (!loops && !s.reverse && s.position < k.position);
}
// One block of a drum sampler voice: true once it may go.
inline bool sampler_cut(Track& k, const Sampler& s, const Table& t, i32 floor) {
    if (new_note(k, s)) {
        k.started = true;
        k.reference = 0;
        k.next = (s.start >> t.shift) + 1;  // the first chunk played in full
    }
    k.count = s.count;
    k.start = s.start;
    k.position = s.position;
    if (floor <= 0 || !s.state || s.reverse) return false;
    // Rule 1: a held note in gate or loop mode is never cut.
    if (s.state == 1 && (s.mode == 0 || s.mode == 3)) return false;
    const u32 chunk = s.position >> t.shift;
    // The reference: each chunk played in full since, at the level it played at.
    while (k.next < chunk && k.next < t.scanned) {
        const i32 v = scaled(s.level, t.chunk_max[k.next]);
        if (v > k.reference) k.reference = v;
        ++k.next;
    }
    if (!t.complete || k.reference <= 0 || chunk >= t.chunks) return false;
    // The loudest the voice can still play: its chunk onward, at the larger of
    // its level and the pad's, doubled for the interpolation's overshoot. Only a
    // held loop goes back to its loop start (rule 1 keeps it); released, stock
    // plays on to the end (11a07c2c wraps only while +0x3c is 1).
    const i32 level = s.level > s.pad_level ? s.level : s.pad_level;
    const i32 bound = scaled(level, t.suffix_max[chunk]);
    const i32 limit = q31::multiply(k.reference, floor);
    return bound <= limit / 2;
}

// The play mode stock's drum sampler reads (11a07c2c): the track's word 3 unless it
// is neutral (0x40000000), else the pad's, scaled to 0..5.
inline u32 sampler_mode(i32 track_word, i32 pad_word) {
    const i32 w = track_word != 0x40000000 ? track_word : pad_word;
    return static_cast<u32>(((w >> 8) * 6) >> 23);
}

}}}
