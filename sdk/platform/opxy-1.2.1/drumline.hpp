// Stock Drumline's second Shift row and per-hit Shift locks: the build option
// drumlineRow2 (tooling/machines.py BUILD_OPTIONS), off by default, because it
// changes stock's own engine 4 ("dbox") on every Drumline track.
// - Row 2 of Shift: SRR, a blank position, FX I and FX II of the selected key.
//   Shift and a press of encoder 2, 3 or 4 switches the rows; Shift and a press
//   of encoder 1 keeps stock's play-mode cycle.
// - Storage: the pad record's word +0x00, which stock's Drumline never reads or
//   writes (stock's samplers keep a frame count there): bits 0..3 SRR, 4..6 FX
//   I's code, 7..9 FX II's. 0 is neutral in each, so a pad from before plays
//   as it did.
// - Audio: at every publication of a Drumline pad (8d5ad776) the GUI carries
//   the fields in payload bits the dbox DSP ignores (drumline_carry); a wrapper
//   of the dbox render (drumline.cpp) keeps each voice's payload from its first
//   block, applies SRR to the voice's mono block after stock's level and
//   envelope, and hands the send codes to the per-key send hooks
//   (drum_send.hpp), which mix them as a padSend kit's.
// - Locks: with a step held, Shift and a turn lock PAN (word 16), VOLUME (17),
//   SRR (18), FX I (19), FX II (32) and RANDOM (33) as per-hit offsets, words
//   no Drumline voice reads; TYPE and the blank turn nothing. Their stored base
//   stays neutral, as a padMix kit's lock words do (drum_locks.hpp).
#pragma once
#include "drum_locks.hpp"
#include "drum_storage.hpp"

namespace opxy { namespace platform {
const u32 kDrumlineEngine = 4;
const u32 kDrumlinePads = 24, kDrumlineRecords = 0x394c, kDrumlineRecordBytes = 0x80;
const u32 kDrumlineSrrMax = 15, kDrumlineSendMax = 7;
// Bits of the pad record's word +0x00 the option uses; the rest stay as found.
const u32 kDrumlineFieldMask = 0x3ffu;

// A pad's three fields.
struct DrumlineFields {
    u32 srr;        // 0 off, 1..15 a hold of srr_hold[srr] samples
    u32 send[2];    // FX I's and FX II's codes, as drum_send.hpp: 0 TRK, 1..7 0..100 percent
};
inline DrumlineFields drumline_fields(u32 word) {
    DrumlineFields f;
    f.srr = word & 15u;
    f.send[0] = (word >> 4) & 7u;
    f.send[1] = (word >> 7) & 7u;
    return f;
}
inline u32 drumline_store(u32 word, const DrumlineFields& f) {
    return (word & ~kDrumlineFieldMask) | (f.srr & 15u) | (f.send[0] & 7u) << 4 | (f.send[1] & 7u) << 7;
}

// The payload carrier. The dbox DSP decodes word 0 as ((w >>> 8) * 66) >>> 23
// (11a06d62..11a06d80), so its bits 0..7 never matter: FX I's code in bits
// 0..2, FX II's in 3..5, and bit 6 set when SRR is on. Then word 6 (random,
// which the DSP squares, 11a06db8) carries SRR in bits 0..3, a change below
// 2^-27 of full scale; at SRR off word 6 is stock's.
const u32 kDrumlineSrrOn = 1u << 6;
inline void drumline_carry(i32* payload, const DrumlineFields& f) {
    payload[0] = static_cast<i32>((static_cast<u32>(payload[0]) & ~0xffu) | (f.send[0] & 7u) | (f.send[1] & 7u) << 3 |
                                  (f.srr ? kDrumlineSrrOn : 0u));
    if (f.srr) payload[6] = static_cast<i32>((static_cast<u32>(payload[6]) & ~15u) | (f.srr & 15u));
}
// What a carried payload holds: both send codes as drum_send_codes gives a
// kit's (FX I in bits 0..2, FX II in 3..5), and SRR.
inline u32 drumline_codes(const i32* payload) { return static_cast<u32>(payload[0]) & 63u; }
inline u32 drumline_srr(const i32* payload) {
    return static_cast<u32>(payload[0]) & kDrumlineSrrOn ? static_cast<u32>(payload[6]) & 15u : 0;
}

// SRR: each held sample stands for this many, as a lower sample rate would;
// the captions are 44.1 kHz over the hold (MD KIT's own table and captions).
inline u32 drumline_srr_hold(u32 srr) {
    switch (srr & 15u) {
    case 0: return 1; case 1: return 2; case 2: return 3; case 3: return 4; case 4: return 5; case 5: return 6;
    case 6: return 8; case 7: return 10; case 8: return 12; case 9: return 16; case 10: return 20;
    case 11: return 24; case 12: return 32; case 13: return 40; case 14: return 48; default: return 64;
    }
}
inline const char* drumline_srr_text(u32 srr) {
    switch (srr & 15u) {
    case 0: return "OFF"; case 1: return "22k"; case 2: return "15k"; case 3: return "11k"; case 4: return "8.8k";
    case 5: return "7.3k"; case 6: return "5.5k"; case 7: return "4.4k"; case 8: return "3.7k"; case 9: return "2.8k";
    case 10: return "2.2k"; case 11: return "1.8k"; case 12: return "1.4k"; case 13: return "1.1k";
    case 14: return "919"; default: return "689";
    }
}
// One block of a voice's mono output, held: every hold-th sample stands until
// the next, `left` frames of the current hold carried between blocks. Runs only
// with SRR on.
inline void drumline_hold(i32* out, u32 frames, u32 hold, i32& held, u32& left) {
    i32 value = held;
    u32 remaining = left;
    for (u32 i = 0; i < frames; ++i) {
        if (!remaining) {
            value = out[i];
            remaining = hold;
        }
        --remaining;
        out[i] = value;
    }
    held = value;
    left = remaining;
}

// A stock dbox voice's object comes from its factory 8d513d1c as 0x84c50b9c +
// index * 0x230, its method table 0x8404ce3c (set at 8d513aa6). Stock sounds
// at most 27 voices; the index, or kDboxVoices for any other object.
const u32 kDboxVoices = 27, kDboxObjects = 0x84c50b9c, kDboxObjectBytes = 0x230, kDboxMethods = 0x8404ce3c;
inline u32 dbox_index(u32 object) {
    const u32 offset = object - kDboxObjects;
    if (object < kDboxObjects || offset >= kDboxVoices * kDboxObjectBytes) return kDboxVoices;
    // offset / 0x230 without a division: (offset / 16) / 35, exact below 2^16 / 19.
    const u32 index = ((offset >> 4) * 1873u) >> 16;
    return index * kDboxObjectBytes == offset ? index : kDboxVoices;
}

// The step locks: per-hit offsets in words no Drumline voice reads.
enum DrumlineLock { kLinePan, kLineVolume, kLineSrr, kLineFx1, kLineFx2, kLineRandom, kDrumlineLocks };
const u32 kDrumlineNoLock = kDrumlineLocks;
inline u32 drumline_lock_word(u32 lock) { return lock < 4 ? 16 + lock : 32 + (lock - 4); }   // 16..19, 32, 33
inline u32 drumline_word_lock(u32 index) {
    return index >= 16 && index < 20 ? index - 16 : index == 32 || index == 33 ? index - 28 : kDrumlineNoLock;
}
// A lock's reach either side of neutral, in the field's own steps: pan +-100,
// volume +-50 dB (its whole span), SRR +-15, a send +-7 codes, random +-99 as
// the page shows it (two of stock's 200 buckets each).
inline i32 drumline_lock_reach(u32 lock) {
    switch (lock) {
    case kLinePan: return 100; case kLineVolume: return 50; case kLineSrr: return 15;
    case kLineFx1: case kLineFx2: return 7; case kLineRandom: return 99; default: return 0;
    }
}
// What Shift and the encoder at a position (0..3) lock on the shown row:
// row 1 TYPE (nothing), RANDOM, PAN, VOLUME; row 2 SRR, the blank (nothing),
// FX I, FX II.
inline u32 drumline_shift_lock(u32 position, bool second) {
    if (second) return position == 0 ? kLineSrr : position == 2 ? kLineFx1 : position == 3 ? kLineFx2 : kDrumlineNoLock;
    return position == 1 ? kLineRandom : position == 2 ? kLinePan : position == 3 ? kLineVolume : kDrumlineNoLock;
}
inline void drumline_offsets(const i32* words, i32* offsets) {
    for (u32 i = 0; i < kDrumlineLocks; ++i)
        offsets[i] = words ? drum_lock_offset(words[i], drumline_lock_reach(i)) : 0;
}

// Stock's own Drumline pad fields: pan the signed byte at +0x1e (-100..100),
// volume the signed byte at +0x1d (-30..+20 dB), random the word +0x14, stock's
// bucket of 200 (8d4d6cc8: (w >>> 8) * 200 >>> 23, stored as its centre).
const i32 kDrumlineVolumeFloor = -30, kDrumlineVolumeCeiling = 20;
const u32 kDrumlineRandomBuckets = 200;
inline i32 drumline_volume(const void* record) {
    return record ? static_cast<signed char>(static_cast<const unsigned char*>(record)[kDrumLevel]) : 0;
}
inline i32 drumline_random_bucket(i32 word) {
    return word <= 0 ? 0 : static_cast<i32>(((static_cast<u32>(word) >> 8) * kDrumlineRandomBuckets) >> 23);
}
inline i32 drumline_random_word(i32 bucket) {
    const i32 width = static_cast<i32>(0x7fffffffu / kDrumlineRandomBuckets);
    return width * bucket + (width >> 1);
}
// The page's 00..99 of each, as stock's dbox page draws them (8d531096..8d5310a6,
// 8d5310dc..8d531106): random (w >>> 8) * 100 >>> 23, pan (p + 100) * 99 / 200,
// volume (v + 30) * 99 / 50, each within 0..99.
inline i32 drumline_shown(i32 value) { return value < 0 ? 0 : value > 99 ? 99 : value; }
inline i32 drumline_random_shown(i32 word) {
    return drumline_shown(static_cast<i32>(((static_cast<u32>(word) >> 8) * 100u) >> 23));
}
inline i32 drumline_pan_shown(i32 pan) { return drumline_shown((pan + 100) * 99 / 200); }
inline i32 drumline_volume_shown(i32 db) { return drumline_shown((db + 30) * 99 / 50); }

// A note's payload with its pad's fields and, at note start, the step's offsets:
// pan into word 5, volume into word 7 through stock's dB-to-gain (`gain`),
// random into word 6, SRR and the sends into the carrier, each clamped to its
// field. Null offsets: the pad's own values.
inline void drumline_fold(const unsigned char* record, const i32* offsets, i32* payload, i32 (*gain)(i32)) {
    DrumlineFields f = drumline_fields(*reinterpret_cast<const u32*>(record));
    if (offsets) {
        if (offsets[kLinePan])
            payload[5] = drum_pan_word(drum_lock_clamp(drum_pan(record) + offsets[kLinePan], -kDrumPanLimit, kDrumPanLimit));
        if (offsets[kLineVolume])
            payload[kDrumGainWord] = gain(drum_lock_clamp(drumline_volume(record) + offsets[kLineVolume],
                                                          kDrumlineVolumeFloor, kDrumlineVolumeCeiling));
        if (offsets[kLineRandom])
            payload[6] = drumline_random_word(drum_lock_clamp(drumline_random_bucket(payload[6]) + 2 * offsets[kLineRandom], 0,
                                                              static_cast<i32>(kDrumlineRandomBuckets) - 1));
        f.srr = static_cast<u32>(drum_lock_clamp(static_cast<i32>(f.srr) + offsets[kLineSrr], 0, kDrumlineSrrMax));
        for (u32 s = 0; s < 2; ++s)
            f.send[s] = static_cast<u32>(drum_lock_clamp(static_cast<i32>(f.send[s]) + offsets[kLineFx1 + s], 0, kDrumlineSendMax));
    }
    drumline_carry(payload, f);
}

// The GUI side, which the shared hooks reach while the build has the option
// (drumline.cpp); null otherwise. The generated startup defines the pointer.
struct Drumline {
    // Whether a track (0..7) is a Drumline track: its own engine is stock's 4,
    // never a kit's alias (sdk_drum_alias).
    bool (*track)(u32 track);
    // A Shift turn of the per-key editor (8c0b147a) on row 2: true when handled.
    bool (*edit)(u32 track, u32 key, u32 control, const u32* event);
    // Shift and a press of encoder 2..4 (button 5..7): switches the rows.
    void (*press)(u32 track, u32 button);
    // Shift and a turn with a step held (8c0b15aa), at position 0..3: the lock
    // or nothing, as stock's editor answers.
    u32 (*lock)(u32 project, u32 track, u32 position, u32 event);
    // A lock word's bucket count, or 0 (8d5ad306).
    u32 (*steps)(u32 index);
    // The payload of a pad slot the builder publishes (8d5ad776); at note start
    // with the step's offsets.
    void (*publish)(u32 track, u32 slot, i32* payload, bool note);
    // The lock words' neutral base: in a track object's stored words (project
    // load, 8d588ec8), and on a track (after scramble, once a frame).
    void (*load)(u32 object);
    void (*neutral)(u32 project, u32 track);
};
}}

extern "C" {
extern const opxy::platform::Drumline* const sdk_drumline __asm__("sdk_drumline");
// Each stock dbox voice's send codes of its last block (drum_send.cpp), as the
// render wrapper keeps them; 0 (TRK) in a build without the option.
extern opxy::u8 sdk_drum_send_dbox[opxy::platform::kDboxVoices] __asm__("sdk_drum_send_dbox");
}

namespace opxy { namespace platform {
// A Drumline track while the build has the option.
inline bool drumline_track(u32 track) { return sdk_drumline && sdk_drumline->track(track); }
}}
